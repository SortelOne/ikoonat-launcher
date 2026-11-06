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
#ifndef XTECH_LUA_AUDIO_H
#define XTECH_LUA_AUDIO_H

struct lua_State;

// smbx2compat: LunaLua's Audio namespace as the global table xtech_lunaAudio
void xtech_lua_register_audio(lua_State *L);
// smbx2compat: sound chunks scripts have open (memory log)
size_t xtech_lua_audioChunkCount();

#endif // XTECH_LUA_AUDIO_H
