#include "lua.h"
#include "lauxlib.h"
#include "hive_env.h"
#include "hive_cell.h"
#include "hive_cell_lib.h"
#include "hive_seri.h"
#include "hive_scheduler.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>

#define DEFAULT_QUEUE 64

struct message {
	int port;
	void * buffer;
};

struct message_queue {
	int cap;
	int head;
	int tail;
	struct message *queue;
};

struct cell {
	int lock;
	int ref;
	lua_State *L;
	struct message_queue mq;
	bool quit;
	bool close;
};

struct cell_ud {
	struct cell * c;
};

static int __cell =0;
#define CELL_TAG (&__cell)

void
cell_grab(struct cell *c) {
	__sync_add_and_fetch(&c->ref,1);
}

void
cell_release(struct cell *c) {
	if (__sync_sub_and_fetch(&c->ref,1) == 0) {
		c->quit = true;
	}
}

static inline void
cell_lock(struct cell *c) {
	while (__sync_lock_test_and_set(&c->lock,1)) {}
}

static inline void
cell_unlock(struct cell *c) {
	__sync_lock_release(&c->lock);
}

static int
ltostring(lua_State *L) {
	char tmp[32];
	struct cell_ud * cud = lua_touserdata(L,1);
	int n = sprintf(tmp,"[cell %p]",cud->c);
	lua_pushlstring(L, tmp,n);
	return 1;
}

static int
lrelease(lua_State *L) {
	struct cell_ud * cud = lua_touserdata(L,1);
	cell_release(cud->c);
	cud->c = NULL;
	return 0;
}

void 
cell_touserdata(lua_State *L, int index, struct cell *c) {
	lua_rawgetp(L, index, c);
	if (lua_isuserdata(L, -1)) {
		return;
	}
	lua_pop(L,1);
	struct cell_ud * cud = lua_newuserdata(L, sizeof(*cud));
	cud->c = c;
	cell_grab(c);
	if (luaL_newmetatable(L, "cell")) {
		lua_pushboolean(L,1);
		lua_rawsetp(L, -2, CELL_TAG);
		lua_pushcfunction(L, ltostring);
		lua_setfield(L, -2, "__tostring");
		lua_pushcfunction(L, lrelease);
		lua_setfield(L, -2, "__gc");
	}
	lua_setmetatable(L, -2);
	lua_pushvalue(L, -1);
	lua_rawsetp(L, index, c);
}

struct cell * 
cell_fromuserdata(lua_State *L, int index) {
	if (lua_type(L, index) != LUA_TUSERDATA) {
		return NULL;
	}
	if (lua_getmetatable(L, index)) {
		lua_rawgetp(L, -1 , CELL_TAG);
		if (lua_toboolean(L, -1)) {
			lua_pop(L,2);
			struct cell_ud * cud = lua_touserdata(L, index);
			return cud->c;
		}
		lua_pop(L,2);
	}
	return NULL;
}

static void
mq_init(struct message_queue *mq) {
	mq->cap = DEFAULT_QUEUE;
	mq->head = 0;
	mq->tail = 0;
	mq->queue = malloc(sizeof(struct message) * DEFAULT_QUEUE);
}

static void
mq_push(struct message_queue *mq, struct message *m) {
	mq->queue[mq->tail] = *m;
	++mq->tail;
	if (mq->tail >= mq->cap) {
		mq->tail = 0;
	}
	if (mq->head == mq->tail) {
		struct message * q = malloc(mq->cap * 2 * sizeof(*q));
		int i;
		for (i=0;i<mq->cap;i++) {
			q[i] = mq->queue[(mq->head+i) % mq->cap];
		}
		mq->head = 0;
		mq->tail = mq->cap;
		mq->cap *=2;
		free(mq->queue);
		mq->queue = q;
	}
}

static int
mq_pop(struct message_queue *mq, struct message *m) {
	if (mq->head == mq->tail)
		return 1;
	*m = mq->queue[mq->head];
	++mq->head;
	if (mq->head >= mq->cap) {
		mq->head = 0;
	}
	return 0;
}

static struct cell *
cell_create() {
	struct cell *c = malloc(sizeof(*c));
	c->lock = 0;
	c->ref = 0;
	c->L = NULL;
	c->quit = false;
	c->close = false;
	mq_init(&c->mq);

	return c;
}

static void
cell_destroy(struct cell *c) {
	assert(c->ref == 0);
	free(c->mq.queue);
	assert(c->L == NULL);
	free(c);
}

static int 
traceback(lua_State *L) {
	const char *msg = lua_tostring(L, 1);
	if (msg)
		luaL_traceback(L, L, msg, 1);
	else {
		lua_pushliteral(L, "(no error message)");
	}
	return 1;
}

static int
lcallback(lua_State *L) {
	int port = lua_tointeger(L,1);
	void *msg = lua_touserdata(L,2);
	int err;
	lua_settop(L,0);
	lua_pushvalue(L, lua_upvalueindex(1));	// traceback
	if (msg == NULL) {
		lua_pushvalue(L, lua_upvalueindex(3));	// dispatcher 3
		lua_pushinteger(L, port);
		err = lua_pcall(L, 1, 0, 1);
	} else {
		lua_pushvalue(L, lua_upvalueindex(3));	// traceback dispatcher 
		lua_pushinteger(L, port);	// traceback dispatcher port
		lua_pushvalue(L, lua_upvalueindex(2));	// traceback dispatcher port data_unpack
		lua_pushlightuserdata(L, msg);	// traceback dispatcher port data_unpack msg
		lua_pushvalue(L, lua_upvalueindex(4));	// traceback dispatcher port data_unpack msg cell_map 
		err = lua_pcall(L, 2, LUA_MULTRET, 1);	
		if (err) {
			printf("Unpack failed : %s\n", lua_tostring(L,-1));
			return 0;
		}
		int n = lua_gettop(L);	// traceback dispatcher ...
		err = lua_pcall(L, n-2, 0, 1);	// traceback 1
	}
	
	if (err) {
		printf("[cell %p] %s\n", lua_touserdata(L, lua_upvalueindex(5)), lua_tostring(L,-1));
	}
	return 0;
}

struct cell *
cell_new(lua_State *L, const char * mainfile) {
	hive_getenv(L, "cell_map");
	int cell_map = lua_absindex(L,-1);	// cell_map
	luaL_requiref(L, "cell.c", cell_lib, 0);	// cell_map cell_lib
	struct cell * c = cell_create();
	c->L = L;
	cell_touserdata(L, cell_map, c);	// cell_map cell_lib cell_ud

	lua_setfield(L, -2, "self");	// cell_map cell_lib

	hive_getenv(L, "system_pointer");
	struct cell * sys = lua_touserdata(L, -1);	// cell_map cell_lib system_cell
	lua_pop(L, 1);	
	if (sys) {
		cell_touserdata(L, cell_map, sys);
		lua_setfield(L, -2, "system");
	}

	lua_pop(L,2);
	lua_pushlightuserdata(L, c);
	hive_setenv(L, "cell_pointer");
	
	int err = luaL_loadfile(L, mainfile);
	if (err) {
		printf("%d : %s\n", err, lua_tostring(L,-1));
		lua_pop(L,1);
		goto _error;
	}

	err = lua_pcall(L, 0, 0, 0);
	if (err) {
		printf("new cell (%s) error %d : %s\n", mainfile, err, lua_tostring(L,-1));
		lua_pop(L,1);
		goto _error;
	}
	lua_pushcfunction(L, traceback);	// upvalue 1
	lua_pushcfunction(L, data_unpack); // upvalue 2
	hive_getenv(L, "dispatcher");	// upvalue 3
	if (!lua_isfunction(L, -1)) {
		printf("set dispatcher first\n");
		goto _error;
	}
	hive_getenv(L, "cell_map");	// upvalue 4
	lua_pushlightuserdata(L, c);
	lua_pushcclosure(L, lcallback, 5);
	return c;
_error:
	scheduler_deletetask(L);
	c->L = NULL;
	cell_destroy(c);

	return NULL;
}

void 
cell_close(struct cell *c) {
	cell_lock(c);
	c->close = true;
	cell_unlock(c);
}

static void
_dispatch(lua_State *L, struct message *m) {
	lua_pushvalue(L, 1);	// dup callback
	lua_pushinteger(L, m->port);
	lua_pushlightuserdata(L, m->buffer);
	lua_call(L, 2, 0);
}

static void
trash_msg(lua_State *L, struct cell *c) {
	// no new message in , because already set c->close
	// don't need lock c later
	struct message m;
	while (!mq_pop(&c->mq, &m)) {
		_dispatch(L, &m);
	}
	// HIVE_PORT 5 : exit 
	// read cell.lua
	m.port = 5;
	m.buffer = NULL;
	_dispatch(L, &m);
}

int 
cell_dispatch_message(struct cell *c) {
	cell_lock(c);
	lua_State *L = c->L;
	if (c->quit) {
		cell_destroy(c);
		return CELL_QUIT;
	}
	if (c->close && L) {
		c->L = NULL;
		cell_grab(c);
		cell_unlock(c);
		trash_msg(L,c);
		cell_release(c);
		scheduler_deletetask(L);
		return CELL_EMPTY;
	}
	struct message m;
	int empty = mq_pop(&c->mq, &m);
	if (empty || L == NULL) {
		cell_unlock(c);
		return CELL_EMPTY;
	} 
	cell_grab(c);
	cell_unlock(c);
	_dispatch(L,&m);
	cell_release(c);

	return CELL_MESSAGE;
}

int 
cell_send(struct cell *c, int port, void *msg) {
	cell_lock(c);
	if (c->quit || c->close) {
		cell_unlock(c);
		return 1;
	}
	struct message m = { port, msg };
	mq_push(&c->mq, &m);
	cell_unlock(c);
	return 0;
}
