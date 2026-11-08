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
#ifndef SCREEN_PAUSE_H
#define SCREEN_PAUSE_H

#include "game_main.h"

namespace PauseScreen
{

extern uint8_t g_pending_action;

void UnlockCheats();
void RequestForcedPause(PauseCode code);

void Init(int plr, bool LegacyPause);

// smbx2compat: Ikoonat's menu (SMBX2 mode, where episodes run their own pause
// menus): the next Init builds Continue, Options and Quit to Library, so
// an episode that greys out its own quit can still be left
void RequestIkoonatMenu();
//! Android's Back key is down (control/keyboard.cpp)
extern bool g_backKeyHeld;

void ControlsLogic();
bool Logic();
void Render();

} // namespace PauseScreen

#endif // #ifndef SCREEN_PAUSE_H
