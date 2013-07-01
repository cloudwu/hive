#include "hive_cell_lib.h"
#include "hive_env.h"
#include "hive_seri.h"
#include "hive_cell.h"
#include "hive_seri.h"

#include "lua.h"
#include "lauxlib.h"

static int
ldispatch(lua_State *L) {
	luaL_checktype(L, 1, LUA_TFUNCTION);
	lua_settop(L,1);
	hive_setenv(L, "dispatcher");
	return 0;
}

static int
lsend(lua_State *L) {
	struct cell * c = cell_fromuserdata(L, 1);
	if (c==NULL) {
		return luaL_error(L, "Need cell object at param 1");
	}
	int port = luaL_checkinteger(L,2);
	if (lua_gettop(L) == 2) {
		if (cell_send(c, port, NULL)) {
			return luaL_error(L, "Cell object %p is closed",c);
		}
		return 0;
	} 
	lua_pushcfunction(L, data_pack);
	lua_replace(L,2);	// cell data_pack ...
	int n = lua_gettop(L);
	lua_call(L, n-2, 1);
	void * msg = lua_touserdata(L,2);
	if (cell_send(c, port, msg)) {
		lua_pushcfunction(L, data_unpack);
		lua_pushvalue(L,2);
		hive_getenv(L, "cell_map");
		lua_call(L,2,0);
		return luaL_error(L, "Cell object %p is closed", c);
	}

	return 0;
}

int
cell_lib(lua_State *L) {
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{ "dispatch", ldispatch },
		{ "send", lsend },
		{ NULL, NULL },
	};
	luaL_newlib(L,l);
	return 1;
}

