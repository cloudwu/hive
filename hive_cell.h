#ifndef hive_cell_h
#define hive_cell_h

#include "lua.h"

struct cell;

#define CELL_MESSAGE 0
#define CELL_EMPTY 1
#define CELL_QUIT 2

struct cell * cell_new(lua_State *L, const char * mainfile);
int cell_dispatch_message(struct cell *c);
int cell_send(struct cell *c, int port, void *msg);
void cell_touserdata(lua_State *L, int index, struct cell *c);
struct cell * cell_fromuserdata(lua_State *L, int index);
void cell_grab(struct cell *c);
void cell_release(struct cell *c);
void cell_close(struct cell *c);

#endif
