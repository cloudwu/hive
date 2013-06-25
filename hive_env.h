#ifndef hive_lua_env_h
#define hive_lua_env_h

#include "lua.h"

void hive_createenv(lua_State *L);
void hive_getenv(lua_State *L, const char * key);
void hive_setenv(lua_State *L, const char * key);
void* hive_copyenv(lua_State *L, lua_State *fromL, const char *key);

#endif
