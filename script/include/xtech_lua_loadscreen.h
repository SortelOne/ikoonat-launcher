#pragma once
#ifndef XTECH_LUA_LOADSCREEN_H
#define XTECH_LUA_LOADSCREEN_H

struct lua_State;

// smbx2compat: SMBX2's loading screens in core mode (LunaLua's Misc/LoadScreen.cpp).
// The episode's loadscreen.lua runs in a second Lua state and draws while a
// level loads: from xtech_lua_loadScreenStart() until the level's first render.

//! Starts the loading screen before a level loads; false if there is none
//! (not in core mode), so the engine shows its own enter screen instead
bool xtech_lua_loadScreenStart();
//! Draws a loading screen frame if one is due; called between load steps.
//! where names the step, for the log's longest gap between frames.
void xtech_lua_loadScreenUpdate(const char *where);
//! The level is about to be drawn: the script finishes (its closing animation
//! may hold the game for a while, as in LunaLua) and the loading screen stops
void xtech_lua_loadScreenEnd();
//! smbx2compat: a loading screen is up (the level hasn't been drawn yet)
bool xtech_lua_loadScreenActive();
//! Closes the loading screen's Lua state (game exit)
void xtech_lua_loadScreenQuit();
// smbx2compat: the loading screen state's Lua heap in KB, -1 without one (memory log)
int xtech_lua_loadScreenLuaKB();

//! xtech_misc_loadScreenUpdate(), for boot.lua's file reads
void xtech_lua_register_loadscreen(lua_State *L);

#endif // XTECH_LUA_LOADSCREEN_H
