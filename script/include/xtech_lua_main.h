#ifndef LUA_MAIN_HHH
#define LUA_MAIN_HHH

extern bool xtech_lua_init();
extern bool xtech_lua_quit();

// Game-level (global) Lua
extern void xtech_lua_loadGame();     // Load game.lua once per session
extern void xtech_lua_unloadGame();   // Reset game.lua loaded flag (for main menu re-entry)
extern bool xtech_lua_gameSave(const std::string& dataPath);  // OnGameSave + serialize JSON
extern bool xtech_lua_gameLoad(const std::string& jsonStr);   // OnGameLoad + deserialize JSON

// Level-level Lua
extern void xtech_lua_load();          // Load level script (calls loadGame + loadLevel)
extern void xtech_lua_loadLevel();     // Sandbox-loaded level script
extern void xtech_lua_unloadLevel();   // Clear level state (no VM destruction)
extern void xtech_lua_reset();         // Unload level (backward compat, no longer destroys VM)

extern void xtech_lua_loop();
extern void xtech_lua_mapEnter();       // Fires OnEnterMap in game.lua
extern void xtech_lua_mapLeave();       // Fires OnLeaveMap in game.lua
extern void xtech_lua_worldMapRender(); // World map render hook
extern void xtech_lua_renderStart();
extern void xtech_lua_renderEnd();
extern void xtech_lua_render(int screenZ);
extern void xtech_lua_renderHud(int screenZ, int numScreens);

struct lua_State;
extern lua_State* xtech_lua_getState();

// Look up a Lua function by name — checks level sandbox first, then _G
// Pushes the function (or nil) onto the stack. Returns true if found.
extern bool xtech_lua_getFunc(const char* funcName);

// smbx2compat: the level runs SMBX2's own scripts (core mode)
extern bool xtech_lua_isSmbx2Core();

// smbx2compat: LunaLua's per-NPC-type vulnerableharmtypes (bit N = harm type N
// kills it), which SMBX2's npcManager.registerHarmTypes sets
extern unsigned xtech_lua_smbx2NpcVulnerableHarmTypes(int npcType);

// smbx2compat: the level is ending; keeps its win type (LevelBeatCode) for
// SMBX2's onExitLevel, before the engine resets it (hub levels do)
extern void xtech_lua_smbx2NoteLevelEnd();

#endif // LUA_MAIN_HHH
