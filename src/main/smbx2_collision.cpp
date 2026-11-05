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

#include <unordered_set>

#include "main/smbx2_collision.h"

size_t g_smbx2FlippedGroupPairs = 0;

static std::unordered_set<uint64_t> s_flipped;

static inline uint64_t s_pairKey(uint32_t i, uint32_t j)
{
    if(i > j)
        std::swap(i, j);
    return (uint64_t(i) << 32) | j;
}

bool SMBX2Collision_IsFlipped(uint32_t i, uint32_t j)
{
    return s_flipped.count(s_pairKey(i, j)) != 0;
}

void SMBX2Collision_SetFlipped(uint32_t i, uint32_t j, bool flipped)
{
    if(flipped)
        s_flipped.insert(s_pairKey(i, j));
    else
        s_flipped.erase(s_pairKey(i, j));

    g_smbx2FlippedGroupPairs = s_flipped.size();
}

void SMBX2Collision_Clear()
{
    s_flipped.clear();
    g_smbx2FlippedGroupPairs = 0;

    for(SMBX2PlayerExt_t &p : g_smbx2PlayerExt)
        p = SMBX2PlayerExt_t();
}
