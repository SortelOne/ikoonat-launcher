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
#ifndef XTECH_LUA_EVENTS_H
#define XTECH_LUA_EVENTS_H

// ============================================================================
// System Event Dispatch — call these from game code (#ifdef ENABLE_XTECH_LUA)
// Each function checks if the corresponding onXxx() Lua function is defined
// and subscribed (for high-frequency events), then calls it safely.
// ============================================================================

// --- NPC Events ---

// Per-frame NPC update. SUBSCRIPTION-REQUIRED: script must call
// xtech_event_subscribe("onNPCUpdate", {npcId, ...}) in onLoad()
void xtech_lua_event_npcUpdate(int permId, int npcId);

void xtech_lua_event_npcDeath(int permId, int npcId, int killerPlayerId);
void xtech_lua_event_npcHurt(int permId, int npcId, int hitterId, int hitType);
void xtech_lua_event_npcActivate(int permId, int npcId);
void xtech_lua_event_npcTalk(int permId, int npcId, int playerId);
void xtech_lua_event_npcTouch(int permId, int npcId, int playerId, int side);
void xtech_lua_event_npcGrab(int permId, int npcId, int playerId, bool fromTop);

// smbx2compat: an NPC was removed and the last one moved into its slot
// (0-based indices, like LunaLua's onPostNPCRearrangeInternal)
void xtech_lua_event_npcRearrange(int newIdx, int oldIdx);

// --- Player Events ---

void xtech_lua_event_playerHurt(int playerId, int causeNpcId);
void xtech_lua_event_playerDeath(int playerId, int causeNpcId);
void xtech_lua_event_playerPowerUp(int playerId, int oldState, int newState);
void xtech_lua_event_playerMount(int playerId, int mountType);
void xtech_lua_event_playerDismount(int playerId, int mountType);
void xtech_lua_event_playerSwitchChar(int playerId, int oldChar, int newChar);
void xtech_lua_event_playerRespawn(int playerId);

// --- Block Events ---

void xtech_lua_event_blockHit(int blockId, int blockType, int hitterId, int hitStyle);
void xtech_lua_event_blockDestroy(int blockId, int blockType, int destroyerId);

// smbx2compat: a P-switch puts a new block into a slot it freed (1-based index,
// like LunaLua's onBlockInvalidateForReuseInternal)
void xtech_lua_event_blockReuse(int idx);

// --- Level / Game Events ---

void xtech_lua_event_levelComplete();
void xtech_lua_event_postMacro();
void xtech_lua_event_levelExit();
void xtech_lua_event_levelFail();
void xtech_lua_event_gameOver();
void xtech_lua_event_pause();
void xtech_lua_event_unpause();

// --- Warp Events ---

void xtech_lua_event_warpEnter(int playerId, int warpId);
void xtech_lua_event_warpExit(int playerId, int warpId);

// --- smbx2compat: SMBX2 core mode's object events ---
// The events LunaLua sends from its SMBX hooks (RuntimeHookHooks.cpp), with the
// engine's indices (1-based NPCs, blocks, players, warps), as LunaLua passes them.
// SMBX2's main_events.lua turns them into objects and sends the onPost* events.
// Each returns true when a script cancelled the event; outside core mode or the
// editor they send nothing and return false. Never reach these from a function
// scripts call through the FFI: they call back into Lua.

bool xtech_lua_event_smbx2NPCKill(int npcIdx, int killReason);
bool xtech_lua_event_smbx2NPCHarm(int npcIdx, int harmType, int culpritIdx);
bool xtech_lua_event_smbx2NPCCollect(int npcIdx, int playerIdx);
bool xtech_lua_event_smbx2BlockHit(int blockIdx, bool fromUpSide, int playerIdx);
bool xtech_lua_event_smbx2BlockRemove(int blockIdx, bool makeEffects);
bool xtech_lua_event_smbx2POW();
bool xtech_lua_event_smbx2PlayerHarm(int playerIdx);
bool xtech_lua_event_smbx2PlayerKill(int playerIdx);
// onWarpEnter; onWarp follows once the player is at the exit
bool xtech_lua_event_smbx2WarpEnter(int warpIdx, int playerIdx);
void xtech_lua_event_smbx2Warp(int warpIdx, int playerIdx);
// onEventDirect (onEvent itself is queued for the next frame); "Level - Start"
// sends onStart first, as LunaLua does
bool xtech_lua_event_smbx2Event(const char *eventName);
// A message box is about to open (npcIdx 0: not an NPC's); cancelled = no box
bool xtech_lua_event_smbx2MessageBox(const char *text, int playerIdx, int npcIdx);
// A player opened the pause menu; cancelled = no menu
bool xtech_lua_event_smbx2Pause(int playerIdx);
// A color switch toggled its blocks (1 yellow, 2 blue, 3 green, 4 red)
void xtech_lua_event_smbx2ColorSwitch(int color);
// A generator made an NPC
void xtech_lua_event_smbx2NPCGenerated(int generatorIdx, int npcIdx);
// The game is about to be saved (SaveGame); SMBX2's savedata.lua writes
// save<slot>-ext.dat on it
void xtech_lua_event_smbx2SaveGame();
// onExplosionInternal: SMBX2's explosion.lua cancels it and makes its own
struct Location_t;
bool xtech_lua_event_smbx2Explosion(const Location_t &loc, int kind, int playerIdx);
// onNPCTransform (LunaLua's NPCTransformationCause: 1 hit, 2 despawn, ...)
void xtech_lua_event_smbx2NPCTransform(int npcIdx, int oldID, int cause);
// Around an engine function that may change an NPC's ID: Begin notes the ID,
// End sends onNPCTransform if it changed. Lua's own NPC:transform marks the
// change handled (LunaLuaMarkNPCTransformationAsHandledByLua), so it isn't
// sent twice.
void xtech_lua_smbx2TransformWatchBegin(int npcIdx);
void xtech_lua_smbx2TransformWatchEnd(int cause);
void xtech_lua_smbx2TransformHandled(int npcIdx, int oldID, int newID);

// --- smbx2compat: keyboard and Misc.pause for SMBX2 core mode ---

// A key went down or up (Windows virtual-key code, as LunaLua passes; ch is
// the character typed, or empty). Only queued: SDL's event loop runs outside
// the frame, so xtech_lua_smbx2SendKeys sends them from the frame.
void xtech_lua_smbx2QueueKey(int vk, bool down, bool repeated, const char *ch);
void xtech_lua_smbx2SendKeys();

// Misc.pause()/unpause(): requests take effect at the start of a frame.
// Returns true while a script holds the game paused; the engine then runs
// only input, onInputUpdate and drawing that frame (LunaLua's runPause).
bool xtech_lua_smbx2LuaPauseFrame();
// The script side of such a frame: queued keys, then onInputUpdate
void xtech_lua_smbx2PausedFrame();
// Right after the engine reads input each game frame: onInputUpdate, onTick
void xtech_lua_smbx2AfterInput();
void xtech_lua_smbx2RequestPause();
void xtech_lua_smbx2RequestUnpause();
bool xtech_lua_smbx2IsPausedByLua();
// A new level: no pause, no queued keys
void xtech_lua_smbx2ResetFrameState();

#endif // XTECH_LUA_EVENTS_H
