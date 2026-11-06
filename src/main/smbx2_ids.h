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
// own NPCs. TheXTech's (NPCs 301-306) now live at 1001-1006 (npc_id.h); level
// files and graphics still name them 301-306. While an episode plays in SMBX2
// mode (main/smbx2_mode.h), IDs 301-306 in a level are SMBX2's NPCs and
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
    return (!g_smbx2Ids && id >= 301 && id <= 306) ? id + 700 : id;
}

// TheXTech's own NPCs: the engine's number -> the one written to files
inline int NPCIDToFile(int type)
{
    return (!g_smbx2Ids && type >= 1001 && type <= 1006) ? type - 700 : type;
}

// The number in the name of an NPC's image and npc-N.txt (npc-301.png for 1001)
inline int NPCFileID(int type)
{
    return (type >= 1001 && type <= 1006) ? type - 700 : type;
}

#endif // SMBX2_IDS_H
