#ifndef hive_scheduler_h
#define hive_scheduler_h

#include "lua.h"

int scheduler_start(lua_State *L);
lua_State * scheduler_newtask(lua_State *L);
void scheduler_deletetask(lua_State *L);
void scheduler_starttask(lua_State *L);

int scheduler_start(lua_State *L);

#endif
