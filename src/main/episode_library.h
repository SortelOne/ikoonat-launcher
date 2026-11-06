/*
 * TheXTech - A platform game engine ported from old source code for VB6
 *
 * Copyright (c) 2009-2011 Andrew Spinks, original VB6 code
 * Copyright (c) 2020-2025 Vitaly Novichkov <admin@wohlnet.ru>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
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
#ifndef EPISODE_LIBRARY_H
#define EPISODE_LIBRARY_H

// smbx2compat: the episode library's own data, <settings>/library.ini: a
// [library] group (where SMBX2's data folder is) and one group per episode,
// named by Library_key. Play time per save slot is TheXTech's own
// (timers<slot>.ini next to the save), so it isn't kept here.

#include <string>
#include <cstdint>

struct LibraryEpisode_t
{
    //! the player's choice: "auto", "smbx" or "smbx2"
    std::string mode = "auto";
    //! the last auto-detect: "smbx" or "smbx2" (empty: never detected)
    std::string detected;
    //! why auto-detect chose it (for the library screen and logs)
    std::string detectedWhy;
    //! when it was last started (seconds since 1970; 0: never)
    int64_t lastPlayed = 0;
    //! the save slot it was last started with (0: none, a level test)
    int lastSlot = 0;
    //! the character player 1 starts as (1-5: Mario, Luigi, Peach, Toad, Link)
    int character = 1;
    //! how many players start it (1 or 2)
    int players = 1;
};

//! The group name for an episode folder: relative to the user folder when it's inside it, else absolute
std::string Library_key(const std::string &episodeDir);

//! Reads an episode's entry; false (and defaults) when it has none
bool Library_get(const std::string &key, LibraryEpisode_t &out);

//! Writes an episode's entry
void Library_set(const std::string &key, const LibraryEpisode_t &e);

//! The episode started most recently (false: none yet)
bool Library_mostRecent(std::string &key, LibraryEpisode_t &out);

//! A [library] setting (the library screen's theme, accent, ...)
std::string Library_setting(const char *name, const std::string &def);
void Library_setSetting(const char *name, const std::string &value);

//! [library] smbx2-data: SMBX2's data folder as the library set it (empty: not set)
std::string Library_smbx2DataDir();

//! Sets [library] smbx2-data (the importer does, once SMBX2's data is copied)
void Library_setSmbx2DataDir(const std::string &dir);

#endif // EPISODE_LIBRARY_H
