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

#include <cstring>
#include <set>
#include <string>
#include <vector>

extern "C"
{
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

#include <luabind/luabind.hpp>
#include <luabind/function.hpp>
#include <luabind/object.hpp>

#include "xtech_lua_main.h"
#include "xtech_lua_bindings.h" // smbx2compat: xtech_lua_memNewFrame

#include <Logger/logger.h>

#include "globals.h" // smbx2compat: Player, LevelEditor
#include "location.h" // smbx2compat

#include "xtech_lua_events.h"


// ============================================================================
// Subscription system for high-frequency events
// ============================================================================

// Set of NPC IDs that are subscribed to onNPCUpdate
static std::set<int> s_npcUpdateSubscribed;

// Generic subscription: event name -> set of IDs
static std::set<std::string> s_subscribedEvents;

// ============================================================================
// Helper: safely call a global Lua function with arguments
// ============================================================================

// Forward-declare helper for safe Lua calls
static bool s_callLuaFunc(lua_State *L, const char *funcName)
{
    if(!L) return false;
    xtech_lua_getFunc(funcName);
    if(!lua_isfunction(L, -1))
    {
        lua_pop(L, 1);
        return false;
    }
    return true;
}

template<typename... Args>
static void s_safeCall(const char *funcName, Args... args)
{
    lua_State *L = xtech_lua_getState();
    if(!L) return;

    // smbx2compat: in core mode these names belong to SMBX2's events (onPause,
    // onBlockHit, ...), which boot.lua sends with LunaLua's arguments
    if(xtech_lua_isSmbx2Core() && strncmp(funcName, "__xtech_", 8) != 0)
        return;

    // Skip if Lua function not defined
    xtech_lua_getFunc(funcName);
    if(!lua_isfunction(L, -1))
    {
        lua_pop(L, 1);
        return;
    }

    // Push args
    int nargs = 0;
    (void)std::initializer_list<int>{(lua_pushinteger(L, (lua_Integer)(args)), ++nargs)...};

    // Protected call
    int result = lua_pcall(L, nargs, 0, 0);
    if(result != 0)
    {
        const char *err = lua_tostring(L, -1);
        pLogWarning("Lua event %s error: %s", funcName, err ? err : "(no message)");
        lua_pop(L, 1);
    }
}


// ============================================================================
// Register subscription function
// ============================================================================

extern "C" int xtech_lua_subscribe_event(lua_State *L)
{
    // xtech_event_subscribe(eventName, {npcId1, npcId2, ...})
    const char *eventName = luaL_checkstring(L, 1);

    if(!lua_istable(L, 2))
    {
        luaL_error(L, "xtech_event_subscribe: second argument must be a table of IDs");
        return 0;
    }

    if(strcmp(eventName, "onNPCUpdate") == 0)
    {
        // Parse the table of NPC IDs
        lua_pushnil(L);
        while(lua_next(L, 2) != 0)
        {
            int npcId = (int)lua_tointeger(L, -1);
            s_npcUpdateSubscribed.insert(npcId);
            lua_pop(L, 1);
        }
    }

    s_subscribedEvents.insert(eventName);
    return 0;
}


// ============================================================================
// NPC Event dispatch
// ============================================================================

void xtech_lua_event_npcUpdate(int permId, int npcId)
{
    // Subscription check for performance
    if(s_npcUpdateSubscribed.find(npcId) == s_npcUpdateSubscribed.end())
        return;

    s_safeCall("onNPCUpdate", permId, npcId);
}

void xtech_lua_event_npcDeath(int permId, int npcId, int killerPlayerId)
{
    s_safeCall("onNPCDeath", permId, npcId, killerPlayerId);
}

void xtech_lua_event_npcHurt(int permId, int npcId, int hitterId, int hitType)
{
    s_safeCall("onNPCHurt", permId, npcId, hitterId, hitType);
}

// smbx2compat: SMBX2 core mode (boot.lua) moves its NPC objects to the new index
void xtech_lua_event_npcRearrange(int newIdx, int oldIdx)
{
    s_safeCall("__xtech_onNPCRearrange", newIdx, oldIdx);
}

void xtech_lua_event_npcActivate(int permId, int npcId)
{
    s_safeCall("onNPCActivate", permId, npcId);
}

void xtech_lua_event_npcTalk(int permId, int npcId, int playerId)
{
    s_safeCall("onNPCTalk", permId, npcId, playerId);
}

void xtech_lua_event_npcTouch(int permId, int npcId, int playerId, int side)
{
    s_safeCall("onNPCTouch", permId, npcId, playerId, side);
}

void xtech_lua_event_npcGrab(int permId, int npcId, int playerId, bool fromTop)
{
    s_safeCall("onNPCGrab", permId, npcId, playerId, (int)fromTop);
}


// ============================================================================
// Player Event dispatch
// ============================================================================

void xtech_lua_event_playerHurt(int playerId, int causeNpcId)
{
    s_safeCall("onPlayerHurt", playerId, causeNpcId);
}

void xtech_lua_event_playerDeath(int playerId, int causeNpcId)
{
    s_safeCall("onPlayerDeath", playerId, causeNpcId);
}

void xtech_lua_event_playerPowerUp(int playerId, int oldState, int newState)
{
    s_safeCall("onPlayerPowerUp", playerId, oldState, newState);
}

void xtech_lua_event_playerMount(int playerId, int mountType)
{
    s_safeCall("onPlayerMount", playerId, mountType);
}

void xtech_lua_event_playerDismount(int playerId, int mountType)
{
    s_safeCall("onPlayerDismount", playerId, mountType);
}

void xtech_lua_event_playerSwitchChar(int playerId, int oldChar, int newChar)
{
    s_safeCall("onPlayerSwitchChar", playerId, oldChar, newChar);
}

void xtech_lua_event_playerRespawn(int playerId)
{
    s_safeCall("onPlayerRespawn", playerId);
}


// ============================================================================
// Block Event dispatch
// ============================================================================

void xtech_lua_event_blockHit(int blockId, int blockType, int hitterId, int hitStyle)
{
    s_safeCall("onBlockHit", blockId, blockType, hitterId, hitStyle);
}

void xtech_lua_event_blockDestroy(int blockId, int blockType, int destroyerId)
{
    s_safeCall("onBlockDestroy", blockId, blockType, destroyerId);
}

// smbx2compat: SMBX2 core mode (boot.lua) drops its Block object for the old block
void xtech_lua_event_blockReuse(int idx)
{
    s_safeCall("__xtech_onBlockReuse", idx);
}


// ============================================================================
// Level / Game Event dispatch
// ============================================================================

void xtech_lua_event_levelComplete()
{
    s_safeCall("onLevelComplete");
}

void xtech_lua_event_postMacro()
{
    s_safeCall("onPostMacro");
}

void xtech_lua_event_levelExit()
{
    s_safeCall("onLevelExit");
}

void xtech_lua_event_levelFail()
{
    s_safeCall("onLevelFail");
}

void xtech_lua_event_gameOver()
{
    s_safeCall("onGameOver");
}

void xtech_lua_event_pause()
{
    s_safeCall("onPause");
}

void xtech_lua_event_unpause()
{
    s_safeCall("onUnpause");
}


// ============================================================================
// Warp Event dispatch
// ============================================================================

void xtech_lua_event_warpEnter(int playerId, int warpId)
{
    s_safeCall("onWarpEnter", playerId, warpId);
}

void xtech_lua_event_warpExit(int playerId, int warpId)
{
    s_safeCall("onWarpExit", playerId, warpId);
}


// ============================================================================
// smbx2compat: SMBX2 core mode's object events (boot.lua's __xtech_on* handlers)
// ============================================================================

static void s_pushArg(lua_State *L, int v)
{
    lua_pushinteger(L, v);
}

static void s_pushArg(lua_State *L, bool v)
{
    lua_pushboolean(L, v);
}

static void s_pushArg(lua_State *L, const char *v)
{
    lua_pushstring(L, v);
}

// Calls a boot.lua handler; true when it reports the event cancelled
template<typename... Args>
static bool s_coreEvent(const char *funcName, Args... args)
{
    if(LevelEditor || !xtech_lua_isSmbx2Core())
        return false;

    lua_State *L = xtech_lua_getState();
    if(!L)
        return false;

    if(!xtech_lua_getFunc(funcName))
    {
        lua_pop(L, 1);
        return false;
    }

    (void)std::initializer_list<int>{(s_pushArg(L, args), 0)...};

    // SMBX2's __callEvent catches the scripts' errors; this only catches boot.lua's
    if(lua_pcall(L, (int)sizeof...(args), 1, 0) != 0)
    {
        const char *err = lua_tostring(L, -1);
        pLogWarning("Lua event %s error: %s", funcName, err ? err : "(no message)");
        lua_pop(L, 1);
        return false;
    }

    bool cancelled = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return cancelled;
}

bool xtech_lua_event_smbx2NPCKill(int npcIdx, int killReason)
{
    return s_coreEvent("__xtech_onNPCKill", npcIdx, killReason);
}

bool xtech_lua_event_smbx2NPCHarm(int npcIdx, int harmType, int culpritIdx)
{
    return s_coreEvent("__xtech_onNPCHarm", npcIdx, harmType, culpritIdx);
}

bool xtech_lua_event_smbx2NPCCollect(int npcIdx, int playerIdx)
{
    return s_coreEvent("__xtech_onNPCCollect", npcIdx, playerIdx);
}

bool xtech_lua_event_smbx2BlockHit(int blockIdx, bool fromUpSide, int playerIdx)
{
    return s_coreEvent("__xtech_onBlockHit", blockIdx, fromUpSide, playerIdx);
}

bool xtech_lua_event_smbx2BlockRemove(int blockIdx, bool makeEffects)
{
    return s_coreEvent("__xtech_onBlockRemove", blockIdx, makeEffects);
}

bool xtech_lua_event_smbx2POW()
{
    return s_coreEvent("__xtech_onPOW");
}

bool xtech_lua_event_smbx2PlayerHarm(int playerIdx)
{
    return s_coreEvent("__xtech_onPlayerHarm", playerIdx);
}

bool xtech_lua_event_smbx2PlayerKill(int playerIdx)
{
    return s_coreEvent("__xtech_onPlayerKill", playerIdx);
}

// the player's section when it entered the warp, so onWarp can tell a section change
static int s_warpEnterSection = -1;

bool xtech_lua_event_smbx2WarpEnter(int warpIdx, int playerIdx)
{
    if(s_coreEvent("__xtech_onWarpEnter", warpIdx, playerIdx))
        return true;

    s_warpEnterSection = Player[playerIdx].Section;
    return false;
}

void xtech_lua_event_smbx2Warp(int warpIdx, int playerIdx)
{
    s_coreEvent("__xtech_onWarp", warpIdx, playerIdx, Player[playerIdx].Section != s_warpEnterSection);
}

bool xtech_lua_event_smbx2Event(const char *eventName)
{
    return s_coreEvent("__xtech_onEvent", eventName);
}

bool xtech_lua_event_smbx2MessageBox(const char *text, int playerIdx, int npcIdx)
{
    if(npcIdx > 0)
        return s_coreEvent("__xtech_onMessageBox", text, playerIdx, npcIdx);

    return s_coreEvent("__xtech_onMessageBox", text, playerIdx);
}

bool xtech_lua_event_smbx2Pause(int playerIdx)
{
    return s_coreEvent("__xtech_onPause", playerIdx);
}

void xtech_lua_event_smbx2ColorSwitch(int color)
{
    s_coreEvent("__xtech_onColorSwitch", color);
}

void xtech_lua_event_smbx2NPCGenerated(int generatorIdx, int npcIdx)
{
    s_coreEvent("__xtech_onNPCGenerated", generatorIdx, npcIdx);
}

void xtech_lua_event_smbx2SaveGame()
{
    s_coreEvent("__xtech_onSaveGame");
}

// LunaLua passes a copy of the explosion's Momentum as a table
static void s_pushArg(lua_State *L, const Location_t *loc)
{
    lua_createtable(L, 0, 6);
    lua_pushnumber(L, (double)loc->X);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, (double)loc->Y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, (double)loc->SpeedX);
    lua_setfield(L, -2, "speedX");
    lua_pushnumber(L, (double)loc->SpeedY);
    lua_setfield(L, -2, "speedY");
    lua_pushnumber(L, (double)loc->Width);
    lua_setfield(L, -2, "width");
    lua_pushnumber(L, (double)loc->Height);
    lua_setfield(L, -2, "height");
}

bool xtech_lua_event_smbx2Explosion(const Location_t &loc, int kind, int playerIdx)
{
    return s_coreEvent("__xtech_onExplosion", &loc, kind, playerIdx);
}

void xtech_lua_event_smbx2NPCTransform(int npcIdx, int oldID, int cause)
{
    s_coreEvent("__xtech_onNPCTransform", npcIdx, oldID, cause);
}

// (NPC index, its ID before) for each engine function being watched; nested
// calls (a hit inside a hit) stack, as in LunaLua's RuntimeHookNpcHarm.cpp
static std::vector<std::pair<int, int>> s_transformWatch;

void xtech_lua_smbx2TransformWatchBegin(int npcIdx)
{
    s_transformWatch.push_back({npcIdx, (int)NPC[npcIdx].Type});
}

void xtech_lua_smbx2TransformWatchEnd(int cause)
{
    if(s_transformWatch.empty())
        return;

    auto w = s_transformWatch.back();
    s_transformWatch.pop_back();

    if((int)NPC[w.first].Type != w.second)
        xtech_lua_event_smbx2NPCTransform(w.first, w.second, cause);
}

void xtech_lua_smbx2TransformHandled(int npcIdx, int oldID, int newID)
{
    for(auto &w : s_transformWatch)
    {
        if(w.first == npcIdx && w.second == oldID)
            w.second = newID;
    }
}


// ============================================================================
// smbx2compat: keyboard events and Misc.pause for SMBX2 core mode
// ============================================================================

struct QueuedKey
{
    int vk;
    bool down;
    bool repeated;
    std::string ch;
};

static std::vector<QueuedKey> s_keyQueue;

void xtech_lua_smbx2QueueKey(int vk, bool down, bool repeated, const char *ch)
{
    // nothing reads them outside core mode, and a long stall shouldn't pile them up
    if(!xtech_lua_isSmbx2Core() || s_keyQueue.size() >= 64)
        return;

    s_keyQueue.push_back({vk, down, repeated, ch ? ch : ""});
}

void xtech_lua_smbx2SendKeys()
{
    if(s_keyQueue.empty())
        return;

    // a handler may queue more (it can't, but keep the loop safe)
    std::vector<QueuedKey> keys;
    keys.swap(s_keyQueue);

    for(const auto &k : keys)
        s_coreEvent("__xtech_onKey", k.vk, k.down, k.repeated, k.ch.c_str());
}

// LunaLua's EventStateMachine: a pause request starts the pause at the next
// frame; while paused, an unpause request ends it at the next frame
static bool s_luaPauseRequest = false;
static bool s_luaUnpauseRequest = false;
static bool s_luaPaused = false;

bool xtech_lua_smbx2LuaPauseFrame()
{
    if(!s_luaPaused && s_luaPauseRequest)
    {
        s_luaPauseRequest = false;
        s_luaPaused = true;
    }
    else if(s_luaPaused && s_luaUnpauseRequest)
    {
        s_luaUnpauseRequest = false;
        s_luaPaused = false;
    }

    return s_luaPaused;
}

void xtech_lua_smbx2PausedFrame()
{
    if(!xtech_lua_isSmbx2Core())
        return;

    xtech_lua_memNewFrame();
    xtech_lua_smbx2SendKeys();
    s_coreEvent("__xtech_onPausedFrame");
}

void xtech_lua_smbx2AfterInput()
{
    s_coreEvent("__xtech_onInputUpdate");
}

void xtech_lua_smbx2RequestPause()
{
    if(!s_luaPaused)
        s_luaPauseRequest = true;

    s_luaUnpauseRequest = false;
}

void xtech_lua_smbx2RequestUnpause()
{
    if(s_luaPaused)
        s_luaUnpauseRequest = true;

    s_luaPauseRequest = false;
}

bool xtech_lua_smbx2IsPausedByLua()
{
    return s_luaPaused;
}

void xtech_lua_smbx2ResetFrameState()
{
    s_luaPauseRequest = false;
    s_luaUnpauseRequest = false;
    s_luaPaused = false;
    s_keyQueue.clear();
    s_transformWatch.clear();
}
