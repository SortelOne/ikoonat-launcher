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
#ifndef SMBX2_IDS_H
#define SMBX2_IDS_H

// smbx2compat: SMBX2 and TheXTech both used the IDs past SMBX 1.3 for their
// own objects. TheXTech's NPCs 301-312 now live at 1001-1012 (npc_id.h), its
// blocks 689, 697 and 701-730 at 2089, 2097 and 2101-2130 (blk_id.h); level
// files and graphics still use the lower numbers. While an episode plays in SMBX2
// mode (main/smbx2_mode.h), those IDs in a level are SMBX2's objects and
// default sprites come from SMBX2's graphics folders.

#include <string>

extern bool g_smbx2Ids;

// Sets g_smbx2Ids for the current episode's mode (LoadGFX and the mode switch call it)
void SMBX2Ids_Detect();

// SMBX2's graphics folder for a category ("npc/", "block/", ...), or empty without SMBX2 data
std::string SMBX2Ids_GfxDir(const char *category);

// TheXTech's own NPCs: the number in a level file -> the engine's
inline int NPCIDFromFile(int id)
{
    return (!g_smbx2Ids && id >= 301 && id <= 312) ? id + 700 : id;
}

// TheXTech's own NPCs: the engine's number -> the one written to files
inline int NPCIDToFile(int type)
{
    return (!g_smbx2Ids && type >= 1001 && type <= 1012) ? type - 700 : type;
}

// The number in the name of an NPC's image and npc-N.txt (npc-301.png for 1001)
inline int NPCFileID(int type)
{
    return (type >= 1001 && type <= 1012) ? type - 700 : type;
}

// TheXTech's own blocks, as numbered in its files (689, 697, 701-730)
inline bool BlockIsXTechFileID(int id)
{
    return id == 689 || id == 697 || (id >= 701 && id <= 730);
}

// TheXTech's own blocks: the number in a level file -> the engine's
inline int BlockIDFromFile(int id)
{
    return (!g_smbx2Ids && BlockIsXTechFileID(id)) ? id + 1400 : id;
}

// TheXTech's own blocks: the engine's number -> the one written to files
inline int BlockIDToFile(int type)
{
    return (!g_smbx2Ids && type > 2000 && BlockIsXTechFileID(type - 1400)) ? type - 1400 : type;
}

// The number in the name of a block's image and block-N.txt (block-701.png for 2101)
inline int BlockFileID(int type)
{
    return (type > 2000 && BlockIsXTechFileID(type - 1400)) ? type - 1400 : type;
}

#endif // SMBX2_IDS_H
