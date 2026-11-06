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
#ifndef LIBRARY_PICKER_H
#define LIBRARY_PICKER_H

// smbx2compat: the system's file picker for the library's Import button
// (macOS: Finder's open panel, library_picker_macos.m)

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * \brief Asks for a folder or zip to import (blocks until the player picks or cancels)
 * \return the path (free() it), or NULL when cancelled or not available
 */
char *LibraryPicker_pickImportSource(void);

//! True when this build has a picker
int LibraryPicker_available(void);

#ifdef __cplusplus
}
#endif

#endif // LIBRARY_PICKER_H
