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

#pragma once
#ifndef XTECH_LUA_DRAW_H
#define XTECH_LUA_DRAW_H

struct lua_State;

// smbx2compat: SMBX2's drawing (Graphics.glDraw, drawImage, Text.print) in core
// mode. Scripts queue draws with a render priority; the level renderer calls
// xtech_lua_drawBelow(p) where LunaLua's priority p begins (right after it
// switches to the draw plane of the objects SMBX2 gives that priority), so each
// queued draw lands between the same objects as in SMBX2.

// SMBX2's priorities for the engine's own layers (LunaLua's RuntimeHookGeneral.cpp)
constexpr double XTECH_LUA_PRI_BGO_BACK   = -95; // backmost BGOs
constexpr double XTECH_LUA_PRI_SIZABLES   = -90;
constexpr double XTECH_LUA_PRI_BGO        = -85;
constexpr double XTECH_LUA_PRI_NPC_BACK   = -75; // NPCs behind blocks
constexpr double XTECH_LUA_PRI_HELD       = -70; // held NPCs, warping players
constexpr double XTECH_LUA_PRI_BLOCKS     = -65;
constexpr double XTECH_LUA_PRI_EFF_BACK   = -60;
constexpr double XTECH_LUA_PRI_NPC_LOW    = -55;
constexpr double XTECH_LUA_PRI_NPC_FROZEN = -50;
constexpr double XTECH_LUA_PRI_NPC        = -45;
constexpr double XTECH_LUA_PRI_PLAYERS    = -35; // mounts, then players at -25
constexpr double XTECH_LUA_PRI_BGO_FRONT  = -20;
constexpr double XTECH_LUA_PRI_NPC_FRONT  = -15;
constexpr double XTECH_LUA_PRI_BLK_FRONT  = -10;
constexpr double XTECH_LUA_PRI_EFF        = -5;
constexpr double XTECH_LUA_PRI_HUD        = 5;
constexpr double XTECH_LUA_PRI_ALL        = 1e300;

// What SMBX2's libraries can take over drawing of (paralx2, betterbgo, sizable, HUD)
enum XTechLuaVanillaDraw
{
    XTECH_LUA_DRAW_BACKGROUND = 0,
    XTECH_LUA_DRAW_BGOS,
    XTECH_LUA_DRAW_SIZABLES,
    XTECH_LUA_DRAW_HUD,
    XTECH_LUA_DRAW_COUNT
};

//! false when a script took over drawing this
bool xtech_lua_drawVanilla(XTechLuaVanillaDraw what);

// Per-frame events of LunaLua's level renderer (xtech_lua_main.cpp)
//! onCameraUpdate, for each camera before any is drawn
void xtech_lua_cameraUpdate(int screenZ);
//! onDraw, after the cameras are updated
void xtech_lua_drawStart();
//! onCameraDraw, then draws queued for this camera may run
void xtech_lua_cameraDraw(int screenZ, int numScreens);
//! The camera is drawn; queued draws wait for the next one
void xtech_lua_cameraEnd();

//! Draws the queued draws with priority < maxPriority not drawn yet in this camera
void xtech_lua_drawBelow(double maxPriority);

// Used by xtech_lua_main.cpp
void xtech_lua_drawCameraStart(int screenZ);
void xtech_lua_drawFrameEnd();
void xtech_lua_drawReset();
void xtech_lua_register_draw(lua_State *L);

#endif // XTECH_LUA_DRAW_H
