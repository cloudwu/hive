#include "lua.h"
#include "lauxlib.h"
#include "hive_scheduler.h"

int
luaopen_hive_core(lua_State *L) {
	luaL_Reg l[] = {
		{ "start", scheduler_start },
		{ NULL, NULL },
	};
	luaL_checkversion(L);
	luaL_newlib(L,l);

	return 1;
}
