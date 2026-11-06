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
#ifndef SMBX2_MODE_H
#define SMBX2_MODE_H

// smbx2compat: which engine an episode plays in. SMBX2 mode runs SMBX2's own
// scripts (core mode) with SMBX2's object IDs and default sprites; SMBX mode
// is plain TheXTech (SMBX 1.3 behavior, the asset pack's sprites). Each
// episode gets one: the player's choice in the library (library.ini), or
// --episode-mode, or auto-detect. The main menu runs no SMBX2 scripts but
// keeps the current sprites (switching them reads every file again).

#include <string>

enum class EpisodeMode
{
    Auto = 0,
    Smbx,
    Smbx2,
};

//! "auto", "smbx", "smbx2"
const char *EpisodeMode_name(EpisodeMode mode);
//! Parses "auto"/"smbx"/"smbx2" (any case); false for anything else
bool EpisodeMode_parse(const std::string &s, EpisodeMode &out);

//! --episode-mode: overrides every episode's mode (Auto: no override)
extern EpisodeMode g_episodeModeOverride;
//! --smbx2-data: SMBX2's data folder (empty: the usual places)
extern std::string g_smbx2DataOverride;

/*!
 * \brief SMBX2's data folder (scripts/, graphics/, sound/, ...), with a slash
 * at the end, or empty when it isn't installed. The first that has
 * scripts/base/engine/main.lua of: --smbx2-data, library.ini's smbx2-data,
 * <user folder>/smbx2, <asset pack>/smbx2.
 */
std::string SMBX2Data_dir();

/*!
 * \brief Core mode's host scripts (boot.lua, ...), with a slash at the end,
 * or empty. <asset pack>/scripts/smbx2core first (a symlink to the repo
 * while developing), then the copy that ships with the app.
 */
std::string SMBX2Core_dir();

/*!
 * \brief Auto-detect: SMBX2 mode when the episode folder has Lua scripts,
 * level/world files saved for SMBX2 (LVLX/WLDX with CPID "SMBX2..."), or
 * custom sprites/configs past SMBX 1.3's and TheXTech's IDs.
 * \param why set to the reason, for logs and the library
 */
EpisodeMode EpisodeMode_detect(const std::string &episodeDir, std::string &why);

/*!
 * \brief Decides the mode of an episode about to start and switches to it
 * (object IDs, default sprites; core mode follows at the next level load).
 * Records it in the library (last played, slot).
 * \param episodeDir the episode's folder (a level test: the level's folder)
 * \param slot the save slot (0: none)
 */
void EpisodeMode_begin(const std::string &episodeDir, int slot);

//! The main menu and its title level: no SMBX2 scripts; the sprites stay
void EpisodeMode_beginMenu();

//! Before the first graphics load (no level test): the sprites of the episode played last
void EpisodeMode_beginStartup();

//! True while an episode in SMBX2 mode is played and SMBX2's data is installed (core mode)
bool EpisodeMode_isSmbx2();

//! True while SMBX2's object IDs and default sprites are in use (g_smbx2Ids)
bool EpisodeMode_idsSmbx2();

#endif // SMBX2_MODE_H
