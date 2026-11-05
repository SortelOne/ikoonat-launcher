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
#ifndef SOUND_SCRIPT_H
#define SOUND_SCRIPT_H

#include <string>

#include "sdl_proxy/mixer.h"

// smbx2compat: what SMBX2's Audio API (script/src/xtech_lua_audio.cpp) needs
// from sound.cpp. Scripts share the engine's one music stream, as in LunaLua.

//! The music stream playing now (the engine's or a script's), or nullptr
Mix_Music *SoundScript_Music();
//! Replaces the music stream with a file, without playing it (Audio.MusicOpen)
void SoundScript_OpenMusic(const std::string &path);
//! Plays the music stream, or resumes it when paused (Audio.MusicPlay)
void SoundScript_PlayMusic(int fadeInMs = 0);
//! Halts the music stream but keeps it open (Audio.MusicStop)
void SoundScript_StopMusic(int fadeOutMs = 0);

//! A music volume (0-128) that replaces the engine's own levels; -1 to drop it
void SoundScript_SetMusicVolume(int volume);
int  SoundScript_GetMusicVolume();

//! The engine no longer starts or stops music in a seized section (-1: all of them)
void SoundScript_SeizeMusic(int section, bool seized);

//! Counts how often a mixer channel has stopped, so a script's handle to a
//! playing sound can tell when its channel went to another sound
unsigned SoundScript_ChannelFinishCount(int channel);

//! Engine sound A's chunk (or its replacement), nullptr when it isn't a chunk
Mix_Chunk *SoundScript_GetSfxChunk(int A);
//! Plays chunk instead of engine sound A; nullptr restores the original
void SoundScript_SetSfxOverride(int A, Mix_Chunk *chunk);
void SoundScript_SetSfxMuted(int A, bool muted);
bool SoundScript_GetSfxMuted(int A);

//! Drops everything above (level end); stops a seized section's music
void SoundScript_Reset();

#endif // SOUND_SCRIPT_H
