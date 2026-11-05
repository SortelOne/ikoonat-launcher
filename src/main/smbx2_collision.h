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
#ifndef SMBX2_COLLISION_H
#define SMBX2_COLLISION_H

// smbx2compat: SMBX2's collision groups and per-object "no collision" flags,
// checked where LunaLua hooks SMBX's collisions (Misc/RuntimeHookComponents/
// RuntimeHookHooks.cpp). Objects keep a group index in their extended fields
// (SMBX2NPCExt_t etc., globals.h); group 0 is the unnamed default. Two objects
// collide unless they share a named group, except for pairs a script flipped
// with Misc.groupsCollide. smbx2compat/core/lunadll.lua keeps the group names
// and mirrors the flipped pairs here.

#include <cstdint>
#include <cstddef>

#include "globals.h"

extern size_t g_smbx2FlippedGroupPairs;

bool SMBX2Collision_IsFlipped(uint32_t i, uint32_t j);
void SMBX2Collision_SetFlipped(uint32_t i, uint32_t j, bool flipped);
void SMBX2Collision_Clear();

inline bool SMBX2Collision_Groups(uint32_t i, uint32_t j)
{
    bool collide = i == 0 || j == 0 || i != j;
    if(g_smbx2FlippedGroupPairs != 0 && SMBX2Collision_IsFlipped(i, j))
        return !collide;
    return collide;
}

// NPC A against NPC B (both ways the same)
inline bool SMBX2Collision_NPCNPC(int A, int B)
{
    const SMBX2NPCExt_t &a = NPC[A].smbx2Ext, &b = NPC[B].smbx2Ext;
    return !a.nonpccollision && !b.nonpccollision && SMBX2Collision_Groups(a.collisionGroup, b.collisionGroup);
}

// NPC A against block B; a block an NPC stands in for has that NPC's group
inline bool SMBX2Collision_NPCBlock(int A, int B)
{
    const SMBX2NPCExt_t &a = NPC[A].smbx2Ext;
    if(a.noblockcollision)
        return false;

    const Block_t &b = Block[B];
    uint32_t group = (b.tempBlockNpcIdx > 0) ? NPC[b.tempBlockNpcIdx].smbx2Ext.collisionGroup : b.smbx2Ext.collisionGroup;
    return SMBX2Collision_Groups(a.collisionGroup, group);
}

// player A against block B
inline bool SMBX2Collision_PlayerBlock(int A, int B)
{
    const SMBX2PlayerExt_t &p = g_smbx2PlayerExt[A];
    return !p.noblockcollision && SMBX2Collision_Groups(p.collisionGroup, Block[B].smbx2Ext.collisionGroup);
}

// player A against NPC B
inline bool SMBX2Collision_PlayerNPC(int A, int B)
{
    const SMBX2PlayerExt_t &p = g_smbx2PlayerExt[A];
    return !p.nonpcinteraction && SMBX2Collision_Groups(p.collisionGroup, NPC[B].smbx2Ext.collisionGroup);
}

// player A against player B
inline bool SMBX2Collision_PlayerPlayer(int A, int B)
{
    const SMBX2PlayerExt_t &a = g_smbx2PlayerExt[A], &b = g_smbx2PlayerExt[B];
    return !a.noplayerinteraction && !b.noplayerinteraction && SMBX2Collision_Groups(a.collisionGroup, b.collisionGroup);
}

#endif // SMBX2_COLLISION_H
