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
#ifndef LEVEL_FILE_H
#define LEVEL_FILE_H

#include <string>
#include <vector>
#include <PGE_File_Formats/lvl_filedata.h>

extern void addMissingLvlSuffix(std::string &fileName);
extern void validateLevelName(std::string &out, const std::string &raw);

//! loads the level
bool OpenLevel(std::string FilePath);
bool OpenLevelData(PGE_FileFormats_misc::TextInput& input, const std::string FilePath = std::string());
void OpenLevelDataPost();
//! Reset everything to zero
void ClearLevel();

//! checks for stars in warps the lead to another level
void FindStars();

// smbx2compat: custom params (JSON) from the loaded level file, for SMBX2 scripts
extern std::string g_levelCustomParams;
//! by section ID
extern std::vector<std::string> g_sectionCustomParams;
//! distinct params of blocks, BGOs and NPCs, indexed by their CustomParams field; [0] is ""
extern std::vector<std::string> g_objectCustomParams;

//! NEW: routines to check if it is possible to convert to legacy file formats and to remove all non-legacy content
// Removed, let PGE-FL handle conversion on its own.
// bool CanConvertLevel(int format, std::string* reasons);
// void ConvertLevel(int format);

#endif // LEVEL_FILE_H

