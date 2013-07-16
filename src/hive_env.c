#include "lua.h"
#include "lauxlib.h"

static int __hive = 0;
#define HIVE_TAG (&__hive)

void
hive_createenv(lua_State *L) {
	lua_newtable(L);
	lua_rawsetp(L, LUA_REGISTRYINDEX, HIVE_TAG);
}

void 
hive_getenv(lua_State *L, const char * key) {
	lua_rawgetp(L, LUA_REGISTRYINDEX, HIVE_TAG);
	lua_getfield(L, -1, key);
	lua_replace(L, -2);
}

void 
hive_setenv(lua_State *L, const char * key) {
	lua_rawgetp(L, LUA_REGISTRYINDEX, HIVE_TAG);
	lua_insert(L, -2);
	lua_setfield(L, -2, key);
	lua_pop(L,1);
}

void *
hive_copyenv(lua_State *L, lua_State *fromL, const char *key) {
	hive_getenv(fromL, key);
	void *p = lua_touserdata(fromL, -1);
	lua_pop(fromL, 1);

	lua_pushlightuserdata(L, p);
	hive_setenv(L, key);

	return p;
}
