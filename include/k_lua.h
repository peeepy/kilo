#ifndef KILO_LUA_H
#define KILO_LUA_H

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

// Initialize Lua and return success/error code
int initLua();

// Get the current Lua state
lua_State* getLuaState(void);

// Reference to the status bar callback function
extern int statusbar_callback_ref;

#endif /* KILO_LUA_H */