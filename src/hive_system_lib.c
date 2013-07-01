#include "lua.h"
#include "lauxlib.h"

#include "hive_env.h"
#include "hive_cell.h"
#include "hive_scheduler.h"
#include "hive_system_lib.h"

static int
llaunch(lua_State *L) {
	const char * filename = luaL_checkstring(L,1);
	lua_State *sL = scheduler_newtask(L);
	struct cell * c = cell_new(sL, filename);
	if (c) {
		cell_touserdata(L, lua_upvalueindex(1), c);
		scheduler_starttask(sL);
		return 1;
	} else {
		return 0;
	}
}

static int
lkill(lua_State *L) {
	struct cell * c = cell_fromuserdata(L, 1);
	if (c) {
		cell_close(c);
		lua_pushboolean(L,1);
		return 1;
	} 
	return 0;
}

static int
linit(lua_State *L) {
	hive_getenv(L, "cell_pointer");
	hive_setenv(L, "system_pointer");
	return 0;
}

int 
cell_system_lib(lua_State *L) {
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{ "kill", lkill },
		{ "init", linit },
		{ NULL, NULL },
	};
	luaL_newlib(L,l);
	hive_getenv(L, "cell_map");
	lua_pushcclosure(L, llaunch, 1);
	lua_setfield(L, -2, "launch");
	return 1;
}

