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
#ifndef SCREEN_LIBRARY_H
#define SCREEN_LIBRARY_H

// smbx2compat: the episode library, Ikoonat Launcher's first screen (in
// place of TheXTech's title menu): a grid of episodes, each episode's page
// (Continue, save slots, its settings), Import and Settings. GameMain runs
// it at the start of each main-menu visit; "Classic menu" in its Settings
// shows TheXTech's menu for that visit (Back on its main page returns).
// Design: smbx2compat/ROADMAP.md, Phase 7, "Episode library app".

#include <string>

namespace LibraryScreen
{

//! The library is the first screen (off for command-line level tests)
extern bool g_enabled;
//! Its frame loop runs while true
extern bool g_LoopActive;
//! This menu visit shows TheXTech's classic menu instead
extern bool g_classicVisit;
//! The classic menu asked to go back to the library
extern bool g_returnRequested;

void Init();
void Loop();

//! A file or folder dropped on the window (SDL_DROPFILE)
void dropFile(const std::string &path);

//! Episode pictures: after an episode starts, a frame of it becomes its picture
void thumbArm(const std::string &episodeDir);
//! Called after each frame the game draws
void thumbAfterFrame();

} // namespace LibraryScreen

#endif // SCREEN_LIBRARY_H
