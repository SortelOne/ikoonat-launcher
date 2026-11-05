/*
 * TheXTech - A platform game engine ported from old source code for VB6
 *
 * Copyright (c) 2009-2011 Andrew Spinks, original VB6 code
 * Copyright (c) 2020-2026 Vitaly Novichkov <admin@wohlnet.ru>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string>
#include <set>
#include <cstdlib>
#include <cstring>

extern "C"
{
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

#include <luabind/luabind.hpp>

#include "script/luna/lunarender.h"
#include <luabind/function.hpp>
#include <luabind/object.hpp>

#include <Logger/logger.h>
#include <AppPath/app_path.h>
#include <Utils/files.h>

#include "globals.h"
#include "global_dirs.h"
#include "config.h"

#include "xtech_lua_main.h"
#include "xtech_lua_bindings.h"
#include "xtech_lua_fileformats.h" // smbx2compat
#include "xtech_lua_audio.h" // smbx2compat
#include "sound_script.h" // smbx2compat
#include "xtech_lua_draw.h" // smbx2compat
#include "xtech_lua_data.h"


// ============================================================================
// Global Lua state
// ============================================================================

static lua_State *g_L = nullptr;
static bool g_luaWorking = false;
static bool g_luaInitialized = false;
static bool g_gameLoaded = false;
static int g_sandboxRef = LUA_NOREF;
static int g_episodeSandboxRef = LUA_NOREF;  // smbx2compat: episode-wide luna.lua
static bool g_compatLoaded = false;          // smbx2compat: shim loaded into _G
static bool g_smbx2Core = false;             // smbx2compat: level runs on SMBX2's own main.lua
static bool s_wasOnMap = false;

// Lua function references (level)
static luabind::object g_luaFunc_onLoad;
static luabind::object g_luaFunc_onLoop;
static luabind::object g_luaFunc_onLoopEnd;
static luabind::object g_luaFunc_onRenderStart;
static luabind::object g_luaFunc_onRenderEnd;
static luabind::object g_luaFunc_onRender;
static luabind::object g_luaFunc_onRenderHud;
// smbx2compat: SMBX2 core mode's per-camera draw events (boot.lua)
static luabind::object g_luaFunc_onCameraUpdate;
static luabind::object g_luaFunc_onDraw;
static luabind::object g_luaFunc_onCameraDraw;

// ============================================================================
// Sandbox: __newindex proxy — whitelist CustomData to _G
// ============================================================================

static int sandbox_newindex(lua_State* L)
{
    // Stack: 1=sandbox, 2=key, 3=value
    const char* key = lua_tostring(L, 2);

    // Whitelist: "CustomData" writes always go to the global table _G
    if(key && strcmp(key, "CustomData") == 0)
    {
        lua_pushvalue(L, 3);
        lua_setglobal(L, key);
        return 0;
    }

    // All other writes stay in the sandbox: sandbox[key] = value
    // Use rawset to avoid re-triggering __newindex (would cause stack overflow!)
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 3);
    lua_rawset(L, 1);
    return 0;
}


// ============================================================================
// Helper: safely call a Lua function
// ============================================================================

// smbx2compat: luabind error handler, so engine-called Lua errors carry a stack traceback
static int luaTracebackHandler(lua_State *L)
{
    const char *msg = lua_tostring(L, 1);
    if(!msg)
        return 1; // non-string error object: pass it through unchanged
    luaL_traceback(L, L, msg, 1);
    return 1;
}

static void pushTracebackHandler(lua_State *L)
{
    lua_pushcfunction(L, luaTracebackHandler);
}

// smbx2compat: per-frame callbacks repeat the same error every tick; log each one once
static void logLuaError(const char *funcName, const char *msg)
{
    static std::set<std::string> s_logged;
    std::string key = std::string(funcName) + ": " + (msg ? msg : "(no message)");
    if(s_logged.count(key))
        return;
    if(s_logged.size() < 1000) // bound memory if a script produces endless unique messages
        s_logged.insert(key);
    pLogWarning("Lua error in %s", key.c_str());
}

static void safeCallLuaFunc(luabind::object &func, const char *funcName)
{
    if(!g_luaWorking || !func.is_valid())
        return;

    try
    {
        func();
    }
    catch(const luabind::error &e)
    {
        // smbx2compat: luabind::error already popped the message; popping again corrupts the VM stack
        logLuaError(funcName, e.what());
    }
    catch(const std::exception &e)
    {
        pLogWarning("Lua exception in %s: %s", funcName, e.what());
    }
    catch(...)
    {
        pLogWarning("Lua unknown exception in %s", funcName);
    }
}

template<typename... Args>
static void safeCallLuaFuncWith(luabind::object &func, const char *funcName, Args... args)
{
    if(!g_luaWorking || !func.is_valid())
        return;

    try
    {
        func(args...);
    }
    catch(const luabind::error &e)
    {
        // smbx2compat: luabind::error already popped the message; popping again corrupts the VM stack
        logLuaError(funcName, e.what());
    }
    catch(const std::exception &e)
    {
        pLogWarning("Lua exception in %s: %s", funcName, e.what());
    }
    catch(...)
    {
        pLogWarning("Lua unknown exception in %s", funcName);
    }
}


// ============================================================================
// Helper: load and execute a .lua file
// ============================================================================

static bool loadAndRunLuaFile(const std::string &filePath, const char *label)
{
    if(filePath.empty())
        return false;

    // Read file
    Files::Data data = Files::load_file(filePath);
    if(data.empty())
    {
        pLogDebug("Lua: no file to load: %s", filePath.c_str());
        return false;
    }

    // Load and run the chunk
    int result = luaL_loadbuffer(g_L, data.c_str(), data.size(), filePath.c_str());
    if(result != 0)
    {
        pLogWarning("Lua: Error loading %s: %s", label,
            lua_tostring(g_L, -1));
        lua_pop(g_L, 1);
        return false;
    }

    result = lua_pcall(g_L, 0, 0, 0);
    if(result != 0)
    {
        pLogWarning("Lua: Error running %s: %s", label,
            lua_tostring(g_L, -1));
        lua_pop(g_L, 1);
        return false;
    }

    pLogInfo("Lua: Loaded and ran script: %s", label);
    return true;
}


// ============================================================================
// Helper: retrieve a global Lua function by name
// ============================================================================

static luabind::object getLuaFunc(const char *funcName)
{
    try
    {
        lua_getglobal(g_L, funcName);
        if(lua_isfunction(g_L, -1))
        {
            luabind::object obj(luabind::from_stack(g_L, -1));
            lua_pop(g_L, 1);
            return obj;
        }
        lua_pop(g_L, 1);
    }
    catch(const luabind::error &) {}
    catch(const std::exception &) {}

    return luabind::object();
}


// ============================================================================
// Public API implementation
// ============================================================================

lua_State* xtech_lua_getState()
{
    return g_L;
}


// Look up a Lua function by name — sandbox first, then _G
bool xtech_lua_getFunc(const char* funcName)
{
    if(!g_L) return false;

    // Try level sandbox first
    if(g_sandboxRef != LUA_NOREF)
    {
        lua_rawgeti(g_L, LUA_REGISTRYINDEX, g_sandboxRef);
        lua_getfield(g_L, -1, funcName);
        lua_remove(g_L, -2); // remove sandbox, keep function/nil
        if(lua_isfunction(g_L, -1))
            return true;
        lua_pop(g_L, 1); // pop nil
    }

    // Fall back to _G
    lua_getglobal(g_L, funcName);
    return lua_isfunction(g_L, -1);
}

// smbx2compat: shared by xtech_lua_init and SMBX2 core mode, which restarts the state per level
static bool createLuaState()
{
    // Create Lua state
    g_L = luaL_newstate();
    if(!g_L)
    {
        pLogWarning("Lua: Failed to create Lua state");
        return false;
    }

    // Open standard libraries
    luaL_openlibs(g_L);

    // Initialize luabind
    luabind::open(g_L);
    luabind::set_pcall_callback(&pushTracebackHandler); // smbx2compat

    // Register game bindings
    xtech_lua_register_bindings(g_L);
    xtech_lua_register_fileformats(g_L); // smbx2compat
    xtech_lua_register_audio(g_L); // smbx2compat
    xtech_lua_register_draw(g_L); // smbx2compat
    return true;
}

bool xtech_lua_init()
{
    if(g_luaInitialized)
        return true;

    // Check if Lua scripting is enabled in config
    if(!g_config.lua_enable_engine)
    {
        pLogDebug("Lua: Lua engine disabled in config");
        return true; // Not a fatal error
    }

    pLogInfo("Lua: Initializing LuaJIT scripting engine...");

    if(!createLuaState())
        return false;

    // Global script (lunaglobal.lua) is deferred to xtech_lua_load()
    // because AppPathManager::assetsRoot() is not ready during init.

    g_luaInitialized = true;
    g_luaWorking = true; // VM is ready even without level script

    pLogInfo("Lua: Initialization complete (VM ready)");
    return true;
}


// ============================================================================
// Game-level: load game.lua once per session
// ============================================================================

void xtech_lua_loadGame()
{
    if(!g_luaInitialized || !g_L || !g_config.lua_enable_engine)
        return;
    if(g_gameLoaded)
        return;

    std::string gamePath = g_dirEpisode.resolveFileCaseExistsAbs("game.lua");
    if(!gamePath.empty())
    {
        pLogInfo("Lua: Loading game.lua from: %s", gamePath.c_str());
        if(loadAndRunLuaFile(gamePath, "game.lua"))
            g_gameLoaded = true;  // only mark loaded if successful
    }
    else
        pLogInfo("Lua: No game.lua found in episode directory, will retry later");
}

void xtech_lua_unloadGame()
{
    // Reset so game.lua will be reloaded next time an episode is entered
    // (needed because the Lua VM now persists across main menu visits)
    g_gameLoaded = false;
    s_wasOnMap = false;
}


// ============================================================================
// Retrieve a function from sandbox by name (sandbox at given registry ref)
// ============================================================================

static luabind::object getSandboxFunc(int sandboxRef, const char* funcName)
{
    lua_rawgeti(g_L, LUA_REGISTRYINDEX, sandboxRef); // [sandbox]
    lua_getfield(g_L, -1, funcName);                  // [sandbox][func/nil]
    if(lua_isfunction(g_L, -1))
    {
        luabind::object obj(luabind::from_stack(g_L, -1));
        lua_pop(g_L, 2);
        return obj;
    }
    lua_pop(g_L, 2);
    return luabind::object();
}


// ============================================================================
// smbx2compat: run a script file in a fresh sandbox; returns registry ref
// ============================================================================

static int runSandboxed(const std::string &path)
{
    Files::Data data = Files::load_file(path);
    if(data.empty())
        return LUA_NOREF;

    if(luaL_loadbuffer(g_L, data.c_str(), data.size(), path.c_str()) != 0)
    {
        pLogWarning("Lua: Error loading %s: %s", path.c_str(), lua_tostring(g_L, -1));
        lua_pop(g_L, 1);
        return LUA_NOREF;
    }

    lua_newtable(g_L);                           // [chunk][sandbox]
    lua_newtable(g_L);                           // [chunk][sandbox][mt]
    lua_pushvalue(g_L, LUA_GLOBALSINDEX);
    lua_setfield(g_L, -2, "__index");            // mt.__index = _G
    lua_pushcfunction(g_L, sandbox_newindex);
    lua_setfield(g_L, -2, "__newindex");
    lua_setmetatable(g_L, -2);                   // [chunk][sandbox]

    int ref = luaL_ref(g_L, LUA_REGISTRYINDEX);  // [chunk]
    lua_rawgeti(g_L, LUA_REGISTRYINDEX, ref);    // [chunk][sandbox]
    lua_setfenv(g_L, -2);                        // [chunk]

    if(lua_pcall(g_L, 0, 0, 0) != 0)
    {
        pLogWarning("Lua: Error running %s: %s", path.c_str(), lua_tostring(g_L, -1));
        lua_pop(g_L, 1);
        luaL_unref(g_L, LUA_REGISTRYINDEX, ref);
        return LUA_NOREF;
    }

    pLogInfo("Lua: Loaded and ran script: %s", path.c_str());
    return ref;
}


// ============================================================================
// smbx2compat: SMBX2 core mode
// ============================================================================

// Core mode runs SMBX2's own scripts/base/engine/main.lua (what LunaLua boots)
// instead of the v0.1 shim. It needs SMBX2's data folder at <assets>/smbx2/ and
// our host layer at <assets>/scripts/smbx2core/boot.lua (smbx2compat/core in the
// repo). SMBX2COMPAT_MODE=shim in the environment forces the shim.
static bool findSmbx2Core(std::string &dataRoot, std::string &coreDir)
{
    const char *mode = std::getenv("SMBX2COMPAT_MODE");
    if(mode && std::strcmp(mode, "shim") == 0)
        return false;

    dataRoot = AppPathManager::assetsRoot() + "smbx2";
    coreDir = AppPathManager::assetsRoot() + "scripts/smbx2core";
    return Files::fileExists(dataRoot + "/scripts/base/engine/main.lua")
        && Files::fileExists(coreDir + "/boot.lua");
}

static void loadSmbx2Level(const std::string &dataRoot, const std::string &coreDir)
{
    // LunaLua gives every level a new Lua state, and main.lua replaces io, os,
    // dofile, ... in _G, so restart ours (bindings and all) the same way
    xtech_lua_clear_delayed_calls();
    lua_close(g_L);
    g_L = nullptr;
    g_gameLoaded = false;
    g_compatLoaded = false;
    if(!createLuaState())
    {
        g_luaInitialized = false;
        return;
    }

    lua_pushstring(g_L, dataRoot.c_str());
    lua_setglobal(g_L, "__xtech_smbx2Path");
    lua_pushstring(g_L, coreDir.c_str());
    lua_setglobal(g_L, "__xtech_coreDir");
    lua_pushboolean(g_L, 0);
    lua_setglobal(g_L, "__xtech_isOverworld");

    // LunaLua knows the episode before main.lua runs (some files resolve paths while loading)
    std::string episodePath = g_dirEpisode.getCurDir();
    if(!episodePath.empty() && episodePath.back() != '/')
        episodePath.push_back('/');
    lua_pushstring(g_L, episodePath.c_str());
    lua_setglobal(g_L, "__xtech_episodePath");
    lua_pushstring(g_L, FileNameFull.c_str());
    lua_setglobal(g_L, "__xtech_levelFile");
    // SMBX2 writes reports into <SMBX>/logs; core mode sends them to our log folder
    lua_pushstring(g_L, AppPathManager::logsDir().c_str());
    lua_setglobal(g_L, "__xtech_logsDir");

    // boot.lua sets up the host side of LunaLua, then runs main.lua
    if(!loadAndRunLuaFile(coreDir + "/boot.lua", "smbx2core/boot.lua"))
        return;

    g_smbx2Core = true;
    g_luaWorking = true;

    luabind::object onInit = getLuaFunc("__onInit");
    safeCallLuaFuncWith(onInit, "__onInit", episodePath, FileNameFull);

    // boot.lua turns these into SMBX2's per-frame events (onStart, onTick, onDraw, ...)
    g_luaFunc_onLoop        = getLuaFunc("__xtech_onLoop");
    g_luaFunc_onRenderStart = getLuaFunc("__xtech_onRenderStart");
    g_luaFunc_onRenderEnd   = getLuaFunc("__xtech_onRenderEnd");
    g_luaFunc_onRenderHud   = getLuaFunc("__xtech_onHUDDraw");
    g_luaFunc_onCameraUpdate = getLuaFunc("__xtech_onCameraUpdate");
    g_luaFunc_onDraw         = getLuaFunc("__xtech_onDraw");
    g_luaFunc_onCameraDraw   = getLuaFunc("__xtech_onCameraDraw");
    pLogInfo("Lua: SMBX2 core started for %s", FileNameFull.c_str());
}


// ============================================================================
// Revised xtech_lua_loadLevel
// ============================================================================

void xtech_lua_loadLevel()
{
    if(!g_luaInitialized || !g_L || !g_config.lua_enable_engine)
        return;

    // Leaving the map: fire OnLeaveMap + reset tracking
    xtech_lua_mapLeave();
    s_wasOnMap = false;

    // Ensure game.lua loaded first
    xtech_lua_loadGame();

    // Clear previous function references
    g_luaFunc_onLoad = luabind::object();
    g_luaFunc_onLoop = luabind::object();
    g_luaFunc_onLoopEnd = luabind::object();
    g_luaFunc_onRenderStart = luabind::object();
    g_luaFunc_onRenderEnd = luabind::object();
    g_luaFunc_onRender = luabind::object();
    g_luaFunc_onRenderHud = luabind::object();
    g_luaFunc_onCameraUpdate = luabind::object(); // smbx2compat
    g_luaFunc_onDraw = luabind::object();
    g_luaFunc_onCameraDraw = luabind::object();
    g_luaWorking = false;

    // Release old sandbox
    if(g_sandboxRef != LUA_NOREF)
    {
        luaL_unref(g_L, LUA_REGISTRYINDEX, g_sandboxRef);
        g_sandboxRef = LUA_NOREF;
    }
    if(g_episodeSandboxRef != LUA_NOREF)
    {
        luaL_unref(g_L, LUA_REGISTRYINDEX, g_episodeSandboxRef);
        g_episodeSandboxRef = LUA_NOREF;
    }

    // smbx2compat: SMBX2 core mode when SMBX2's scripts are installed, else the shim
    std::string smbx2Data, smbx2Core;
    if(findSmbx2Core(smbx2Data, smbx2Core))
    {
        loadSmbx2Level(smbx2Data, smbx2Core);
        return;
    }

    // smbx2compat: load the SMBX2 compatibility shim into _G once
    if(!g_compatLoaded)
    {
        std::string shim = g_dirEpisode.resolveFileCaseExistsAbs("smbx2compat.lua");
        if(shim.empty())
        {
            std::string assetShim = AppPathManager::assetsRoot() + "scripts/smbx2compat.lua";
            if(Files::fileExists(assetShim))
                shim = assetShim;
        }
        if(!shim.empty())
            g_compatLoaded = loadAndRunLuaFile(shim, "smbx2compat.lua");
    }

    // Find level script (SMBX2 uses luna.lua; the fork's own names still work)
    std::string levelPath = g_dirCustom.resolveFileCaseExistsAbs("luna.lua");
    if(levelPath.empty())
        levelPath = g_dirCustom.resolveFileCaseExistsAbs("level.lua");
    if(levelPath.empty())
        levelPath = g_dirCustom.resolveFileCaseExistsAbs("lunadll.lua");

    // smbx2compat: SMBX2 also runs an episode-wide luna.lua in every level
    std::string episodePath = g_compatLoaded ? g_dirEpisode.resolveFileCaseExistsAbs("luna.lua") : std::string();

    if(levelPath.empty() && episodePath.empty())
        return;

    if(g_compatLoaded)
    {
        lua_getglobal(g_L, "__smbx2_beginLevel");
        if(lua_isfunction(g_L, -1))
        {
            lua_pushstring(g_L, g_dirCustom.getCurDir().c_str());
            lua_pushstring(g_L, g_dirEpisode.getCurDir().c_str());
            if(lua_pcall(g_L, 2, 0, 0) != 0)
            {
                pLogWarning("Lua: __smbx2_beginLevel failed: %s", lua_tostring(g_L, -1));
                lua_pop(g_L, 1);
            }
        }
        else
            lua_pop(g_L, 1);
    }

    if(!episodePath.empty())
        g_episodeSandboxRef = runSandboxed(episodePath);
    if(!levelPath.empty())
        g_sandboxRef = runSandboxed(levelPath);

    // Hooks are looked up in the level sandbox; fall back to the episode one
    if(g_sandboxRef == LUA_NOREF && g_episodeSandboxRef != LUA_NOREF)
    {
        lua_rawgeti(g_L, LUA_REGISTRYINDEX, g_episodeSandboxRef);
        g_sandboxRef = luaL_ref(g_L, LUA_REGISTRYINDEX);
    }
    if(g_sandboxRef == LUA_NOREF)
        return;

    if(g_compatLoaded)
    {
        lua_getglobal(g_L, "__smbx2_attach");
        if(lua_isfunction(g_L, -1))
        {
            int nargs = 0;
            if(g_episodeSandboxRef != LUA_NOREF) { lua_rawgeti(g_L, LUA_REGISTRYINDEX, g_episodeSandboxRef); nargs++; }
            if(!levelPath.empty()) { lua_rawgeti(g_L, LUA_REGISTRYINDEX, g_sandboxRef); nargs++; }
            if(lua_pcall(g_L, nargs, 0, 0) != 0)
            {
                pLogWarning("Lua: __smbx2_attach failed: %s", lua_tostring(g_L, -1));
                lua_pop(g_L, 1);
            }
        }
        else
            lua_pop(g_L, 1);
    }

    pLogInfo("Lua: Loaded and ran level script");
    g_luaWorking = true;

    // Retrieve hook functions from sandbox
    g_luaFunc_onLoad       = getSandboxFunc(g_sandboxRef, "onLoad");
    g_luaFunc_onLoop       = getSandboxFunc(g_sandboxRef, "onLoop");
    g_luaFunc_onLoopEnd    = getSandboxFunc(g_sandboxRef, "onLoopEnd");
    g_luaFunc_onRenderStart = getSandboxFunc(g_sandboxRef, "onRenderStart");
    g_luaFunc_onRenderEnd  = getSandboxFunc(g_sandboxRef, "onRenderEnd");
    g_luaFunc_onRender     = getSandboxFunc(g_sandboxRef, "onRender");
    g_luaFunc_onRenderHud  = getSandboxFunc(g_sandboxRef, "onRenderHud");

    // Call onLoad immediately
    safeCallLuaFunc(g_luaFunc_onLoad, "onLoad");
}


// ============================================================================
// Unload level script (no VM destruction)
// ============================================================================

void xtech_lua_unloadLevel()
{
    if(!g_luaInitialized || !g_L)
        return;

    // Fire onLevelExit before tearing down (death, completion, quit all go through here)
    // smbx2compat: in core mode boot.lua sends SMBX2's onExitLevel/onExit instead
    xtech_lua_getFunc(g_smbx2Core ? "__xtech_onExit" : "onLevelExit");
    g_smbx2Core = false;
    if(lua_isfunction(g_L, -1))
    {
        if(lua_pcall(g_L, 0, 0, 0) != 0)
        {
            pLogWarning("Lua: Error in onLevelExit: %s", lua_tostring(g_L, -1));
            lua_pop(g_L, 1);
        }
    }
    else
        lua_pop(g_L, 1);

    // Clear async delayed calls
    xtech_lua_clear_delayed_calls();

    // smbx2compat: seized music, sound overrides and mutes end with the level
    SoundScript_Reset();
    // smbx2compat: so do queued draws, scripts' pictures and drawing take-overs
    xtech_lua_drawReset();

    // Clear function references
    g_luaFunc_onLoad = luabind::object();
    g_luaFunc_onLoop = luabind::object();
    g_luaFunc_onLoopEnd = luabind::object();
    g_luaFunc_onRenderStart = luabind::object();
    g_luaFunc_onRenderEnd = luabind::object();
    g_luaFunc_onRenderHud = luabind::object();
    g_luaFunc_onCameraUpdate = luabind::object(); // smbx2compat
    g_luaFunc_onDraw = luabind::object();
    g_luaFunc_onCameraDraw = luabind::object();
    g_luaWorking = false;

    // Release sandbox
    if(g_sandboxRef != LUA_NOREF)
    {
        luaL_unref(g_L, LUA_REGISTRYINDEX, g_sandboxRef);
        g_sandboxRef = LUA_NOREF;
    }
    if(g_episodeSandboxRef != LUA_NOREF)
    {
        luaL_unref(g_L, LUA_REGISTRYINDEX, g_episodeSandboxRef);
        g_episodeSandboxRef = LUA_NOREF;
    }
}


// ============================================================================
// Backward-compatible xtech_lua_load (game + level)
// ============================================================================

void xtech_lua_load()
{
    xtech_lua_loadGame();
    xtech_lua_loadLevel();
}


// ============================================================================
// Revised xtech_lua_reset (no VM destruction)
// ============================================================================

void xtech_lua_reset()
{
    xtech_lua_unloadLevel();
}


// ============================================================================
// Game-level hooks: OnGameSave / OnGameLoad
// ============================================================================

void xtech_lua_callGameSave()
{
    if(!g_luaInitialized || !g_L)
        return;

    lua_getglobal(g_L, "OnGameSave");
    if(!lua_isfunction(g_L, -1))
    {
        lua_pop(g_L, 1);
        return;
    }

    // Call OnGameSave(), expects return table
    if(lua_pcall(g_L, 0, 1, 0) != 0)
    {
        pLogWarning("Lua: Error in OnGameSave: %s", lua_tostring(g_L, -1));
        lua_pop(g_L, 1);
        return;
    }

    // Table is left on stack for caller to serialize
    // Caller must pop when done
}


void xtech_lua_callGameLoad()
{
    if(!g_luaInitialized || !g_L)
        return;

    if(lua_gettop(g_L) < 1)
        return;

    int tableRef = luaL_ref(g_L, LUA_REGISTRYINDEX);

    lua_getglobal(g_L, "OnGameLoad");
    if(!lua_isfunction(g_L, -1))
    {
        lua_pop(g_L, 1);
        luaL_unref(g_L, LUA_REGISTRYINDEX, tableRef);
        return;
    }

    lua_rawgeti(g_L, LUA_REGISTRYINDEX, tableRef);
    luaL_unref(g_L, LUA_REGISTRYINDEX, tableRef);

    if(lua_pcall(g_L, 1, 0, 0) != 0)
    {
        pLogWarning("Lua: Error in OnGameLoad: %s", lua_tostring(g_L, -1));
        lua_pop(g_L, 1);
    }
}


// Complete save cycle: call OnGameSave, serialize to JSON, write file
bool xtech_lua_gameSave(const std::string& dataPath)
{
    xtech_lua_loadGame();  // ensure game.lua loaded
    xtech_lua_callGameSave();

    if(!g_L || !lua_istable(g_L, -1))
    {
        if(g_L && lua_gettop(g_L) > 0) lua_pop(g_L, 1);
        return false;
    }

    std::string json = lua_table_to_json(g_L, lua_gettop(g_L));
    lua_pop(g_L, 1);

    if(json.empty() || json == "null")
        return false;

    // Write JSON to file
    FILE* f = Files::utf8_fopen(dataPath.c_str(), "w");
    if(!f) return false;
    fwrite(json.c_str(), 1, json.size(), f);
    fclose(f);
    return true;
}


// Complete load cycle: parse JSON, call OnGameLoad
bool xtech_lua_gameLoad(const std::string& jsonStr)
{
    if(!g_L) return false;
    xtech_lua_loadGame();  // ensure game.lua loaded

    if(jsonStr.empty())
    {
        lua_newtable(g_L);
    }
    else
    {
        json_to_lua_table(g_L, jsonStr);
    }

    xtech_lua_callGameLoad();
    return true;
}


// ============================================================================
// World map render hook
// ============================================================================

// ============================================================================
// Map enter/leave hooks
// ============================================================================

void xtech_lua_mapEnter()
{
    if(!g_luaInitialized || !g_L) return;
    xtech_lua_loadGame();

    lua_getglobal(g_L, "OnEnterMap");
    if(lua_isfunction(g_L, -1))
    {
        if(lua_pcall(g_L, 0, 0, 0) != 0)
        {
            pLogWarning("Lua: Error in OnEnterMap: %s", lua_tostring(g_L, -1));
            lua_pop(g_L, 1);
        }
    }
    else
        lua_pop(g_L, 1);
}

void xtech_lua_mapLeave()
{
    if(!g_luaInitialized || !g_L) return;
    if(!s_wasOnMap) return;  // only fire if we were actually on the map

    lua_getglobal(g_L, "OnLeaveMap");
    if(lua_isfunction(g_L, -1))
    {
        if(lua_pcall(g_L, 0, 0, 0) != 0)
        {
            pLogWarning("Lua: Error in OnLeaveMap: %s", lua_tostring(g_L, -1));
            lua_pop(g_L, 1);
        }
    }
    else
        lua_pop(g_L, 1);
}

void xtech_lua_worldMapRender()
{
    if(!g_luaInitialized || !g_L)
        return;

    xtech_lua_loadGame();  // ensure game.lua loaded

    // Fire OnEnterMap on first render frame after entering the map
    if(!s_wasOnMap)
    {
        s_wasOnMap = true;
        xtech_lua_mapEnter();
    }

    lua_getglobal(g_L, "OnWorldMapRender");
    if(lua_isfunction(g_L, -1))
    {
        if(lua_pcall(g_L, 0, 0, 0) != 0)
        {
            pLogWarning("Lua: Error in OnWorldMapRender: %s", lua_tostring(g_L, -1));
            lua_pop(g_L, 1);
        }
    }
    else
        lua_pop(g_L, 1);
}


void xtech_lua_loop()
{
    if(!g_luaWorking)
        return;

    // Process async delayed calls
    xtech_lua_process_delayed_calls();

    safeCallLuaFunc(g_luaFunc_onLoop, "onLoop");
}


void xtech_lua_renderStart()
{
    // Start the render frame if LunaLua is not managing it
    if(!g_config.luna_enable_engine)
        Renderer::Get().StartFrameRender();
    safeCallLuaFunc(g_luaFunc_onRenderStart, "onRenderStart");
}


void xtech_lua_renderEnd()
{
    safeCallLuaFunc(g_luaFunc_onRenderEnd, "onRenderEnd");
    xtech_lua_drawFrameEnd(); // smbx2compat: SMBX2's draws last one frame
    // End the render frame and clean up render ops if LunaLua is not managing it
    if(!g_config.luna_enable_engine)
    {
        Renderer::Get().EndFrameRender();
        Renderer::Get().ClearQueue();
    }
}


void xtech_lua_render(int screenZ)
{
    safeCallLuaFuncWith(g_luaFunc_onRender, "onRender", screenZ);
}


void xtech_lua_renderHud(int screenZ, int numScreens)
{
    safeCallLuaFuncWith(g_luaFunc_onRenderHud, "onRenderHud", screenZ, numScreens);
    // smbx2compat: SMBX2 draws below priority 5 before the HUD
    xtech_lua_drawBelow(XTECH_LUA_PRI_HUD);
}


// smbx2compat: LunaLua's level render events (core mode)
void xtech_lua_cameraUpdate(int screenZ)
{
    safeCallLuaFuncWith(g_luaFunc_onCameraUpdate, "onCameraUpdate", screenZ);
}

void xtech_lua_drawStart()
{
    safeCallLuaFunc(g_luaFunc_onDraw, "onDraw");
}

void xtech_lua_cameraDraw(int screenZ, int numScreens)
{
    xtech_lua_drawCameraStart(screenZ);
    safeCallLuaFuncWith(g_luaFunc_onCameraDraw, "onCameraDraw", screenZ, numScreens);
}


bool xtech_lua_quit()
{
    if(!g_luaInitialized)
        return true;

    // Call onLoopEnd one last time if available
    safeCallLuaFunc(g_luaFunc_onLoopEnd, "onLoopEnd");

    // Unload level state
    xtech_lua_unloadLevel();

    // Release sandbox
    if(g_sandboxRef != LUA_NOREF)
    {
        luaL_unref(g_L, LUA_REGISTRYINDEX, g_sandboxRef);
        g_sandboxRef = LUA_NOREF;
    }
    if(g_episodeSandboxRef != LUA_NOREF)
    {
        luaL_unref(g_L, LUA_REGISTRYINDEX, g_episodeSandboxRef);
        g_episodeSandboxRef = LUA_NOREF;
    }

    g_luaWorking = false;
    g_gameLoaded = false;
    g_compatLoaded = false; // smbx2compat: the shim lives in the state being destroyed

    // Destroy Lua state
    if(g_L)
    {
        lua_close(g_L);
        g_L = nullptr;
    }

    g_luaInitialized = false;
    pLogInfo("Lua: Shutdown complete");

    return true;
}
