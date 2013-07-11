#include "hive_socket_lib.h"
#include "socket_poll.h"

#include "lua.h"
#include "lauxlib.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#define MAX_ID 0x7fffffff
#define DEFAULT_SOCKET 128
#define READ_BUFFER 4000
#define MAX_EVENT 32
#define BACKLOG 32

#define STATUS_INVALID 0
#define STATUS_HALFCLOSE 1
#define STATUS_SUSPEND 2

struct write_buffer {
	struct write_buffer * next;
	char *ptr;
	size_t sz;
	void *buffer;
};

struct socket {
	int fd;
	int id;
	short status;
	short listen;
	struct write_buffer * head;
	struct write_buffer * tail;
};

struct socket_pool {
	poll_fd fd;
	struct event ev[MAX_EVENT];
	int id;
	int count;
	int cap;
	struct socket ** s;
};

static int
linit(lua_State *L) {
	struct socket_pool * sp = lua_touserdata(L, lua_upvalueindex(1));
	if (sp->s) {
		return luaL_error(L, "Don't init socket twice");
	}
	sp->fd = sp_create();
	sp->count = 0;
	sp->cap = DEFAULT_SOCKET;
	sp->id = 1;
	sp->s = malloc(sp->cap * sizeof(struct socket *));
	int i;
	for (i=0;i<sp->cap;i++) {
		sp->s[i] = malloc(sizeof(struct socket));
		memset(sp->s[i],0, sizeof(struct socket));
	}
	return 0;
}

static inline struct socket_pool *
get_sp(lua_State *L) {
	struct socket_pool * pool = lua_touserdata(L, lua_upvalueindex(1));
	if (pool->s == NULL) {
		luaL_error(L, "Init socket first");
	}
	return pool;
}

static int
lexit(lua_State *L) {
	struct socket_pool * pool = lua_touserdata(L, 1);
	int i;
	if (pool->s) {
		for (i=0;i<pool->cap;i++) {
			if (pool->s[i]->status != STATUS_INVALID && pool->s[i]->fd >=0) {
				closesocket(pool->s[i]->fd);
			}
			free(pool->s[i]);
		}
		free(pool->s);
		pool->s = NULL;
	}
	pool->cap = 0;
	pool->count = 0;
	if (!sp_invalid(pool->fd)) {
		pool->fd = sp_release(pool->fd);
	}
	pool->id = 1;

	return 0;
}

static void
expand_pool(struct socket_pool *p) {
	struct socket ** s = malloc(p->cap * 2 * sizeof(struct socket *));
	memset(s, 0, p->cap * 2 * sizeof(struct socket *));
	int i;
	for (i=0;i<p->cap;i++) {
		int nid = p->s[i]->id % (p->cap *2);
		assert(s[nid] == NULL);
		s[nid] = p->s[i];
	}
	for (i=0;i<p->cap * 2;i++) {
		if (s[i] == NULL) {
			s[i] = malloc(sizeof(struct socket));
			memset(s[i],0,sizeof(struct socket));
		}
	}
	free(p->s);
	p->s = s;
	p->cap *=2;
}

static int
new_socket(struct socket_pool *p, int sock) {
	int i;
	if (p->count >= p->cap) {
		expand_pool(p);
	}
	for (i=0;i<p->cap;i++) {
		int id = p->id + i;
		int n = id % p->cap;
		struct socket * s = p->s[n];
		if (s->status == STATUS_INVALID) {
			if (sp_add(p->fd, sock, s)) {
				goto _error;
			}
			s->status = STATUS_SUSPEND;
			s->listen = 0;
			sp_nonblocking(sock);
			int keepalive = 1; 
			setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepalive , sizeof(keepalive));
			s->fd = sock;
			s->id = id;
			p->count++;
			if (++p->id > MAX_ID) {
				p->id = 1;
			}
			assert(s->head == NULL && s->tail == NULL);
			return id;
		}
	}
_error:
	closesocket(sock);
	return -1;
}

static int
lconnect(lua_State *L) {
	int status;
	struct addrinfo ai_hints;
	struct addrinfo *ai_list = NULL;
	struct addrinfo *ai_ptr = NULL;

	struct socket_pool * pool = get_sp(L);
	const char * host = luaL_checkstring(L,1);
	const char * port = luaL_checkstring(L,2);

	memset( &ai_hints, 0, sizeof( ai_hints ) );
	ai_hints.ai_family = AF_UNSPEC;
	ai_hints.ai_socktype = SOCK_STREAM;
	ai_hints.ai_protocol = IPPROTO_TCP;

	status = getaddrinfo( host, port, &ai_hints, &ai_list );
	if ( status != 0 ) {
		return 0;
	}
	int sock= -1;
	for	( ai_ptr = ai_list;	ai_ptr != NULL;	ai_ptr = ai_ptr->ai_next ) {
		sock = socket( ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol );
		if ( sock < 0 ) {
			continue;
		}
		status = connect( sock,	ai_ptr->ai_addr, ai_ptr->ai_addrlen	);
		if ( status	!= 0 ) {
			close(sock);
			sock = -1;
			continue;
		}
		break;
	}

	freeaddrinfo( ai_list );

	if (sock < 0) {
		return 0;
	}

	int fd = new_socket(pool, sock);
	lua_pushinteger(L,fd);
	return 1;
}

static void
force_close(struct socket *s, struct socket_pool *p) {
	struct write_buffer *wb = s->head;
	while (wb) {
		struct write_buffer *tmp = wb;
		wb = wb->next;
		free(tmp->buffer);
		free(tmp);
	}
	s->head = s->tail = NULL;
	s->status = STATUS_INVALID;
	sp_del(p->fd, s->fd);
	closesocket(s->fd);
	--p->count;
}

static int
lclose(lua_State *L) {
	struct socket_pool * p = get_sp(L);
	int id = luaL_checkinteger(L,1);
	struct socket * s = p->s[id % p->cap];
	if (id != s->id) {
		return luaL_error(L, "Close invalid socket %d", id);
	}
	if (s->head == NULL) {
		force_close(s,p);
	} else {
		s->status = STATUS_HALFCLOSE;
	}
	return 0;
}

static inline void
result_n(lua_State *L, int n) {
	lua_rawgeti(L,1,n);
	if (lua_istable(L,-1)) {
		return;
	}
	lua_pop(L,1);
	lua_rawgeti(L,2,n);
	if (!lua_istable(L,-1)) {
		lua_pop(L,1);
		lua_newtable(L);
	}
	lua_pushvalue(L,-1);
	lua_rawseti(L,1, n);
}

static inline void
remove_after_n(lua_State *L, int n) {
	int t = lua_rawlen(L,1);
	int i;
	for (i=n;i<=t;i++) {
		lua_rawgeti(L,1,i);
		if (lua_istable(L,-1)) {
			lua_rawseti(L,2,i);
		} else {
			lua_pop(L,1);
		}
		lua_pushnil(L);
		lua_rawseti(L,1,i);
	}
}

static int
push_result(lua_State *L, int idx, struct socket *s, struct socket_pool *p) {
	int ret = 0;
	for (;;) {
		char * buffer = malloc(READ_BUFFER);
		int r = 0;
		for (;;) {
			r = recv(s->fd, buffer, READ_BUFFER,0);
			if (r == -1) {
				switch(errno) {
				case EAGAIN:
					free(buffer);
					return ret;
				case EINTR:
					continue;
				}
				r = 0;
				break;
			}
			break;
		}
		if (r == 0) {
			force_close(s,p);
			free(buffer);
			buffer = NULL;
		}

		if (s->status == STATUS_HALFCLOSE) {
			free(buffer);
		} else {
			result_n(L, idx);
			++ret;
			++idx;
			lua_pushinteger(L, s->id);
			lua_rawseti(L, -2, 1);
			lua_pushinteger(L, r);
			lua_rawseti(L, -2, 2);
			lua_pushlightuserdata(L, buffer);
			lua_rawseti(L, -2, 3);
			lua_pop(L,1);
		}
		if (r < READ_BUFFER)
			return ret;
	}
}

static int
accept_result(lua_State *L, int idx, struct socket *s, struct socket_pool *p) {
	int ret = 0;
	for (;;) {
		struct sockaddr_in remote_addr;
		socklen_t len = sizeof(struct sockaddr_in);
		int client_fd = accept(s->fd , (struct sockaddr *)&remote_addr ,  &len);
		if (client_fd < 0) {
			return ret;
		}
		int id = new_socket(p, client_fd);
		if (id < 0) {
			return ret;
		}

		result_n(L, idx);
		++ret;
		++idx;
		lua_pushinteger(L, s->id);
		lua_rawseti(L, -2, 1);
		lua_pushinteger(L, id);
		lua_rawseti(L, -2, 2);
		lua_pushstring(L, inet_ntoa(remote_addr.sin_addr));
		lua_rawseti(L, -2, 3);
		lua_pop(L,1);
	}
}

static void
sendout(struct socket_pool *p, struct socket *s) {
	while (s->head) {
		struct write_buffer * tmp = s->head;
		for (;;) {
			int sz = send(s->fd, tmp->ptr, tmp->sz,0);
			if (sz < 0) {
				switch(errno) {
				case EINTR:
					continue;
				case EAGAIN:
					return;
				}
				force_close(s,p);
				return;
			}
			if (sz != tmp->sz) {
				tmp->ptr += sz;
				tmp->sz -= sz;
				return;
			}
			break;
		}
		s->head = tmp->next;
		free(tmp->buffer);
		free(tmp);
	}
	s->tail = NULL;
	sp_write(p->fd, s->fd, s, false);
}

static int
lpoll(lua_State *L) {
	struct socket_pool * p = get_sp(L);
	luaL_checktype(L,1,LUA_TTABLE);
	int timeout = luaL_optinteger(L,2,100);
	lua_settop(L,1);
	lua_rawgeti(L,1,0);
	if (lua_isnil(L,-1)) {
		lua_pop(L,1);
		lua_newtable(L);
		lua_pushvalue(L,-1);
		lua_rawseti(L,1,0);
	} else {
		luaL_checktype(L,2,LUA_TTABLE);
	}

	int n = sp_wait(p->fd, p->ev, MAX_EVENT, timeout); 
	int i;
	int t = 1;
	for (i=0;i<n;i++) {
		struct event *e = &p->ev[i];
		if (e->read) {
			struct socket * s= e->s;
			if (s->listen) {
				t += accept_result(L, t, e->s, p);
			} else {
				t += push_result(L, t, e->s, p);
			}
		}
		if (e->write) {
			struct socket *s = e->s;
			sendout(p, s);
			if (s->status == STATUS_HALFCLOSE && s->head == NULL) {
				force_close(s, p);
			}
		}
	}

	remove_after_n(L,t);
	lua_pushinteger(L, t-1);
	return 1;
}

static int
lsend(lua_State *L) {
	struct socket_pool * p = get_sp(L);
	int id = luaL_checkinteger(L,1);
	int sz = luaL_checkinteger(L,2);
	void * msg = lua_touserdata(L,3);

	struct socket * s = p->s[id % p->cap];
	if (id != s->id) {
		free(msg);
		return luaL_error(L,"Write to invalid socket %d", id);
	}
	if (s->status != STATUS_SUSPEND) {
		free(msg);
//		return luaL_error(L,"Write to closed socket %d", id);
		return 0;
	}
	if (s->head) {
		struct write_buffer * buf = malloc(sizeof(*buf));
		buf->ptr = msg;
		buf->buffer = msg;
		buf->sz = sz;
		assert(s->tail != NULL);
		assert(s->tail->next == NULL);
		buf->next = s->tail->next;
		s->tail->next = buf;
		s->tail = buf;
		return 0;
	}

	char * ptr = msg;

	for (;;) {
		int wt = send(s->fd, ptr, sz,0);
		if (wt < 0) {
			switch(errno) {
			case EINTR:
				continue;
			}
			break;
		}
		if (wt == sz) {
			return 0;
		}
		sz-=wt;
		ptr+=wt;

		break;
	}

	struct write_buffer * buf = malloc(sizeof(*buf));
	buf->next = NULL;
	buf->ptr = ptr;
	buf->sz = sz;
	buf->buffer = msg;
	s->head = s->tail = buf;

	sp_write(p->fd, s->fd, s, true);

	return 0;
}

// buffer support

struct socket_buffer {
	int size;
	int head;
	int tail;
};

static struct socket_buffer *
new_buffer(lua_State *L, int sz) {
	struct socket_buffer * buffer = lua_newuserdata(L, sizeof(*buffer) + sz);
	buffer->size = sz;
	buffer->head = 0;
	buffer->tail = 0;

	return buffer;
}

static void
copy_buffer(struct socket_buffer * dest, struct socket_buffer * src) {
	char * ptr = (char *)(src+1);
	if (src->tail >= src->head) {
		int sz = src->tail - src->head;
		memcpy(dest+1, ptr+ src->head, sz);
		dest->tail = sz;
	} else {
		char * d = (char *)(dest+1);
		int part = src->size - src->head;
		memcpy(d, ptr + src->head, part);
		memcpy(d + part, ptr , src->tail);
		dest->tail = src->tail + part;
	}
}

static void
append_buffer(struct socket_buffer * buffer, char * msg, int sz) {
	char * dst = (char *)(buffer + 1);
	if (sz + buffer->tail < buffer->size) {
		memcpy(dst + buffer->tail , msg, sz);
		buffer->tail += sz;
	} else {
		int part = buffer->size - buffer->tail;
		memcpy(dst + buffer->tail , msg, part);
		memcpy(dst, msg + part, sz - part);
		buffer->tail = sz - part;
	}
}

static int
lpush(lua_State *L) {
	struct socket_buffer * buffer = lua_touserdata(L, 1);
	int bytes = 0;
	if (buffer) {
		bytes = buffer->tail - buffer->head;
		if (bytes < 0) {
			bytes += buffer->size;
		}
	}
	void * msg = lua_touserdata(L,2);
	if (msg == NULL) {
		lua_settop(L,1);
		lua_pushinteger(L,bytes);
		return 2;
	}

	int sz = luaL_checkinteger(L,3);

	if (buffer == NULL) {
		struct socket_buffer * nbuf = new_buffer(L, sz * 2);
		append_buffer(nbuf, msg, sz);
	} else if (sz + bytes >= buffer->size) {
		struct socket_buffer * nbuf = new_buffer(L, (sz + bytes) * 2);
		copy_buffer(nbuf, buffer);
		append_buffer(nbuf, msg, sz);
	} else {
		lua_settop(L,1);
		append_buffer(buffer, msg, sz);
	}
	lua_pushinteger(L, sz + bytes);
	free(msg);
	return 2;
}

static int
lpop(lua_State *L) {
	struct socket_buffer * buffer = lua_touserdata(L, 1);
	if (buffer == NULL) {
		return 0;
	}
	int sz = luaL_checkinteger(L, 2);
	int	bytes = buffer->tail - buffer->head;
	if (bytes < 0) {
		bytes += buffer->size;
	}

	if (sz > bytes || bytes == 0) {
		lua_pushnil(L);
		lua_pushinteger(L, bytes);
		return 2;
	}

	if (sz == 0) {
		sz = bytes;
	}

	char * ptr = (char *)(buffer+1);
	if (buffer->size - buffer->head >=sz) {
		lua_pushlstring(L, ptr + buffer->head, sz);
		buffer->head+=sz;
	} else {
		luaL_Buffer b;
		luaL_buffinit(L, &b);
		luaL_addlstring(&b, ptr + buffer->head, buffer->size - buffer->head);
		buffer->head = sz - (buffer->size - buffer->head);
		luaL_addlstring(&b, ptr, buffer->head);
		luaL_pushresult(&b);
	}

	bytes -= sz;

	lua_pushinteger(L,bytes);
	if (bytes == 0) {
		buffer->head = buffer->tail = 0;
	}
	
	return 2;
}

static inline int
check_sep(struct socket_buffer *buffer, int from, const char * sep, int sz) {
	const char * ptr = (const char *)(buffer+1);
	int i;
	for (i=0;i<sz;i++) {
		int index = from + i;
		if (index >= buffer->size) {
			index = 0;
		}
		if (ptr[index] != sep[i]) {
			return 0;
		}
	}
	return 1;
}

static int
lreadline(lua_State *L) {
	struct socket_buffer * buffer = lua_touserdata(L, 1);
	if (buffer == NULL) {
		return 0;
	}
	size_t len = 0;
	const char *sep = luaL_checklstring(L,2,&len);
	int read = !lua_toboolean(L,3);
	int	bytes = buffer->tail - buffer->head;

	if (bytes < 0) {
		bytes += buffer->size;
	}

	int i;
	for (i=0;i<=bytes-(int)len;i++) {
		int index = buffer->head + i;
		if (index >= buffer->size) {
			index -= buffer->size;
		}
		if (check_sep(buffer, index, sep, (int)len)) {
			if (read == 0) {
				lua_pushboolean(L,1);
			} else {
				if (i==0) {
					lua_pushlstring(L, "", 0);
				} else {
					const char * ptr = (const char *)(buffer+1);
					if (--index < 0) {
						index = buffer->size -1;
					}
					if (index < buffer->head) {
						luaL_Buffer b;
						luaL_buffinit(L, &b);
						luaL_addlstring(&b, ptr + buffer->head, buffer->size-buffer->head);
						luaL_addlstring(&b, ptr, index+1);
						luaL_pushresult(&b);
					} else {
						lua_pushlstring(L, ptr + buffer->head, index-buffer->head+1);
					}
					++index;
				}
				index+=len;
				if (index >= buffer->size) {
					index-=buffer->size;
				}
				buffer->head = index;
			}
			return 1;
		}
	}
	return 0;
}

static int
lsendpack(lua_State *L) {
	size_t len = 0;
	const char * str = luaL_checklstring(L, 1, &len);
	lua_pushinteger(L, (int)len);
	void * msg = malloc(len);
	memcpy(msg, str, len);
	lua_pushlightuserdata(L, msg);

	return 2;
}

static int
lfreepack(lua_State *L) {
	void * msg = lua_touserdata(L,1);
	free(msg);
	return 0;
}

// server support

static int
llisten(lua_State *L) {
	struct socket_pool * p = get_sp(L);
	// only support ipv4
	int port = 0;
	size_t len = 0;
	const char * name = luaL_checklstring(L, 1, &len);
	char binding[len+1];
	memcpy(binding, name, len+1);
	char * portstr = strchr(binding,':');
	uint32_t addr = INADDR_ANY;
	if (portstr == NULL) {
		port = strtol(binding, NULL, 10);
		if (port <= 0) {
			return luaL_error(L, "Invalid address %s", name);
		}
	} else {
		port = strtol(portstr + 1, NULL, 10);
		if (port <= 0) {
			return luaL_error(L, "Invalid address %s", name);
		}
		portstr[0] = '\0';
		addr=inet_addr(binding);
	}
	int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	int id = new_socket(p, listen_fd);
	if (id < 0) {
		return luaL_error(L, "Create socket %s failed", name);
	}
	struct socket * s = p->s[id % p->cap];
	s->listen = 1;
	int reuse = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (void *)&reuse, sizeof(int));

	struct sockaddr_in my_addr;
	memset(&my_addr, 0, sizeof(struct sockaddr_in));
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(port);
	my_addr.sin_addr.s_addr = addr;
	if (bind(listen_fd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) == -1) {
		force_close(s,p);
		return luaL_error(L, "Bind %s failed", name);
	}
	if (listen(listen_fd, BACKLOG) == -1) {
		force_close(s,p);
		return luaL_error(L, "Listen %s failed", name);
	}

	lua_pushinteger(L, id);

	return 1;
}

int 
socket_lib(lua_State *L) {
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{ "init", linit },
		{ "connect", lconnect },
		{ "close", lclose },
		{ "poll", lpoll },
		{ "send", lsend },
		{ "sendpack", lsendpack },
		{ "freepack", lfreepack },
		{ "push", lpush },
		{ "pop", lpop },
		{ "readline", lreadline },
		{ "listen", llisten },
		{ NULL, NULL },
	};
	luaL_newlibtable(L,l);
	struct socket_pool *sp = lua_newuserdata(L, sizeof(*sp));
	memset(sp, 0, sizeof(*sp));
	sp->fd = sp_init();
	lua_newtable(L);
	lua_pushcfunction(L, lexit);
	lua_setfield(L, -2, "__gc");
	lua_setmetatable(L, -2);

	luaL_setfuncs(L,l,1);
	return 1;
}


