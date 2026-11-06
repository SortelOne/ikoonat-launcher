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

// smbx2compat: LunaLua's Audio namespace (LuaProxyAudio.cpp) on the engine's
// mixer, which is the same library LunaLua uses (MixerX). Registered as the
// global table xtech_lunaAudio; smbx2compat/core/host.lua builds Audio from it
// and resolves file paths before they get here.
//
// A Mix_Chunk is a userdata viewing a chunk; chunks opened here are cached by
// path and freed with the Lua state. SfxPlayObj* return a PlayingSfxInstance,
// which stops controlling its channel once the channel finishes (the engine
// counts that in SoundScript_ChannelFinishCount).

#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
}

#include <Logger/logger.h>
#include <Utils/files.h>
#include "sdl_proxy/sdl_timer.h"

#include "globals.h"
#include "config.h"
#include "sound.h"
#include "sound_script.h"

#include "xtech_lua_audio.h"

namespace
{

const char *const CHUNK_MT = "Mix_Chunk";
const char *const INSTANCE_MT = "PlayingSfxInstance";
const char *const OWNER_MT = "xtech_lunaAudio.owner";
// registry table: lightuserdata(Mix_Chunk*) -> its userdata, so a chunk stays one value
const char *const CHUNK_VIEWS = "xtech_lunaAudio.views";

// Chunks opened by scripts, freed when the Lua state closes (OWNER_MT's __gc)
std::unordered_map<std::string, Mix_Chunk*> s_chunks;

struct ChunkView
{
    Mix_Chunk *chunk;
};

struct SfxInstance
{
    int channel;
    unsigned finishCount;
};

// MusicClock: seconds of music played since MusicPlay started the track
double s_musicClock = 0.0;
Uint32 s_musicClockTicks = 0;


// Volumes from scripts are 0-128; the player's SFX volume setting still applies
int sfxVolume(int volume)
{
    if(volume < 0)
        volume = 0;
    if(volume > MIX_MAX_VOLUME)
        volume = MIX_MAX_VOLUME;
    return volume * g_config.audio_sfx_volume / 100;
}

int sfxVolumeArg(lua_State *L, int arg)
{
    return sfxVolume(luaL_optint(L, arg, MIX_MAX_VOLUME));
}

void pushChunk(lua_State *L, Mix_Chunk *chunk)
{
    if(!chunk)
    {
        lua_pushnil(L);
        return;
    }

    lua_getfield(L, LUA_REGISTRYINDEX, CHUNK_VIEWS);
    lua_pushlightuserdata(L, chunk);
    lua_rawget(L, -2);
    if(!lua_isnil(L, -1))
    {
        lua_remove(L, -2);
        return;
    }
    lua_pop(L, 1);

    auto *view = static_cast<ChunkView*>(lua_newuserdata(L, sizeof(ChunkView)));
    view->chunk = chunk;
    luaL_getmetatable(L, CHUNK_MT);
    lua_setmetatable(L, -2);

    lua_pushlightuserdata(L, chunk);
    lua_pushvalue(L, -2);
    lua_rawset(L, -4);
    lua_remove(L, -2);
}

Mix_Chunk *toChunk(lua_State *L, int arg)
{
    auto *view = static_cast<ChunkView*>(luaL_checkudata(L, arg, CHUNK_MT));
    return view->chunk;
}

Mix_Chunk *optChunk(lua_State *L, int arg)
{
    if(lua_isnoneornil(L, arg))
        return nullptr;
    return toChunk(L, arg);
}

// "sound5" -> 5 (LunaLua's aliases for SMBX's sounds)
int aliasSound(lua_State *L, int arg)
{
    const char *alias = luaL_checkstring(L, arg);
    if(std::strncmp(alias, "sound", 5) != 0)
        return 0;
    return std::atoi(alias + 5);
}


// ---- Mix_Chunk ----

int chunkIndex(lua_State *L)
{
    Mix_Chunk *chunk = toChunk(L, 1);
    const char *key = luaL_checkstring(L, 2);

    if(std::strcmp(key, "allocated") == 0)
        lua_pushinteger(L, chunk->allocated);
    else if(std::strcmp(key, "alen") == 0)
        lua_pushnumber(L, chunk->alen);
    else if(std::strcmp(key, "volume") == 0)
        lua_pushinteger(L, chunk->volume);
    else
        lua_pushnil(L);

    return 1;
}

int chunkNewIndex(lua_State *L)
{
    Mix_Chunk *chunk = toChunk(L, 1);
    const char *key = luaL_checkstring(L, 2);

    if(std::strcmp(key, "volume") == 0)
        Mix_VolumeChunk(chunk, luaL_checkint(L, 3));
    else
        return luaL_error(L, "Mix_Chunk.%s cannot be written", key);

    return 0;
}

// With sound off there's no mixer to load into, but scripts still expect a
// chunk (LunaLua always has one); this one plays nothing
Mix_Chunk *silentChunk()
{
    static Mix_Chunk chunk;
    chunk.allocated = 1;
    chunk.abuf = nullptr;
    chunk.alen = 0;
    chunk.volume = MIX_MAX_VOLUME;
    return &chunk;
}

int ownerGc(lua_State *)
{
    // overrides may point at these chunks
    SoundScript_Reset();

    for(auto &c : s_chunks)
        Mix_FreeChunk(c.second);
    s_chunks.clear();

    return 0;
}

int sfxOpen(lua_State *L)
{
    std::string path = luaL_checkstring(L, 1);

    auto found = s_chunks.find(path);
    if(found != s_chunks.end())
    {
        pushChunk(L, found->second);
        return 1;
    }

    SDL_RWops *f = Files::open_file(path, "rb");
    if(!f)
        return luaL_error(L, "Could not open sound file: %s", path.c_str());

    if(!Mix_QuerySpec(nullptr, nullptr, nullptr))
    {
        SDL_RWclose(f);
        pushChunk(L, silentChunk());
        return 1;
    }

    Mix_Chunk *chunk = Mix_LoadWAV_RW(f, 1);
    if(!chunk)
        return luaL_error(L, "Could not load sound %s: %s", path.c_str(), Mix_GetError());

    s_chunks.insert({path, chunk});
    pushChunk(L, chunk);
    return 1;
}

int playSFX(lua_State *L)
{
    sfxOpen(L);
    Mix_PlayChannelVol(-1, toChunk(L, -1), 0, sfxVolume(MIX_MAX_VOLUME));
    return 0;
}

int newMixChunk(lua_State *L)
{
    lua_pushnil(L);
    return 1;
}

int clearSFXBuffer(lua_State *)
{
    return 0; // chunks stay cached until the level ends
}


// ---- Channels ----

int sfxPlayCh(lua_State *L)
{
    lua_pushinteger(L, Mix_PlayChannelVol(luaL_checkint(L, 1), toChunk(L, 2), luaL_checkint(L, 3), sfxVolume(MIX_MAX_VOLUME)));
    return 1;
}

int sfxPlayChVol(lua_State *L)
{
    lua_pushinteger(L, Mix_PlayChannelVol(luaL_checkint(L, 1), toChunk(L, 2), luaL_checkint(L, 3), sfxVolumeArg(L, 4)));
    return 1;
}

int sfxPlayChTimed(lua_State *L)
{
    lua_pushinteger(L, Mix_PlayChannelTimedVolume(luaL_checkint(L, 1), toChunk(L, 2), luaL_checkint(L, 3),
                                                  luaL_checkint(L, 4), sfxVolume(MIX_MAX_VOLUME)));
    return 1;
}

int sfxPlayChTimedVol(lua_State *L)
{
    lua_pushinteger(L, Mix_PlayChannelTimedVolume(luaL_checkint(L, 1), toChunk(L, 2), luaL_checkint(L, 3),
                                                  luaL_checkint(L, 4), sfxVolumeArg(L, 5)));
    return 1;
}

int sfxFadeInCh(lua_State *L)
{
    lua_pushinteger(L, Mix_FadeInChannelVolume(luaL_checkint(L, 1), toChunk(L, 2), luaL_checkint(L, 3),
                                               luaL_checkint(L, 4), sfxVolume(MIX_MAX_VOLUME)));
    return 1;
}

int sfxFadeInChVol(lua_State *L)
{
    lua_pushinteger(L, Mix_FadeInChannelVolume(luaL_checkint(L, 1), toChunk(L, 2), luaL_checkint(L, 3),
                                               luaL_checkint(L, 4), sfxVolumeArg(L, 5)));
    return 1;
}

int sfxFadeInChTimed(lua_State *L)
{
    lua_pushinteger(L, Mix_FadeInChannelTimedVolume(luaL_checkint(L, 1), toChunk(L, 2), luaL_checkint(L, 3),
                                                    luaL_checkint(L, 4), luaL_checkint(L, 5), sfxVolume(MIX_MAX_VOLUME)));
    return 1;
}

int sfxFadeInChTimedVol(lua_State *L)
{
    lua_pushinteger(L, Mix_FadeInChannelTimedVolume(luaL_checkint(L, 1), toChunk(L, 2), luaL_checkint(L, 3),
                                                    luaL_checkint(L, 4), luaL_checkint(L, 5), sfxVolumeArg(L, 6)));
    return 1;
}

int sfxPause(lua_State *L)
{
    Mix_Pause(luaL_checkint(L, 1));
    return 0;
}

int sfxResume(lua_State *L)
{
    Mix_Resume(luaL_checkint(L, 1));
    return 0;
}

int sfxStop(lua_State *L)
{
    lua_pushinteger(L, Mix_HaltChannel(luaL_checkint(L, 1)));
    return 1;
}

int sfxExpire(lua_State *L)
{
    lua_pushinteger(L, Mix_ExpireChannel(luaL_checkint(L, 1), luaL_checkint(L, 2)));
    return 1;
}

int sfxFadeOut(lua_State *L)
{
    lua_pushinteger(L, Mix_FadeOutChannel(luaL_checkint(L, 1), luaL_checkint(L, 2)));
    return 1;
}

int sfxIsPlaying(lua_State *L)
{
    lua_pushinteger(L, Mix_Playing(luaL_checkint(L, 1)));
    return 1;
}

int sfxIsPaused(lua_State *L)
{
    lua_pushinteger(L, Mix_Paused(luaL_checkint(L, 1)));
    return 1;
}

int sfxIsFading(lua_State *L)
{
    int channel = luaL_checkint(L, 1);
    lua_pushinteger(L, Mix_FadingChannel(channel < 0 ? 1 : channel)); // LunaLua's anti-crash
    return 1;
}

int sfxChannelVolume(lua_State *L)
{
    int volume = luaL_checkint(L, 2);
    lua_pushinteger(L, Mix_Volume(luaL_checkint(L, 1), volume < 0 ? -1 : sfxVolume(volume)));
    return 1;
}

int sfxSetPanning(lua_State *L)
{
    lua_pushinteger(L, Mix_SetPanning(luaL_checkint(L, 1), (Uint8)luaL_checkint(L, 2), (Uint8)luaL_checkint(L, 3)));
    return 1;
}

int sfxSetDistance(lua_State *L)
{
    lua_pushinteger(L, Mix_SetDistance(luaL_checkint(L, 1), (Uint8)luaL_checkint(L, 2)));
    return 1;
}

int sfxSet3DPosition(lua_State *L)
{
    lua_pushinteger(L, Mix_SetPosition(luaL_checkint(L, 1), (Sint16)luaL_checkint(L, 2), (Uint8)luaL_checkint(L, 3)));
    return 1;
}

int sfxReverseStereo(lua_State *L)
{
    lua_pushinteger(L, Mix_SetReverseStereo(luaL_checkint(L, 1), luaL_checkint(L, 2)));
    return 1;
}

int mixedSfxVolume(lua_State *L)
{
    lua_pushinteger(L, Mix_MasterVolume(luaL_optint(L, 1, -1)));
    return 1;
}


// ---- PlayingSfxInstance ----

// Starts a chunk on the first free channel and returns a handle to it. The
// channel's finish count is read before playing, so the handle can tell
// when its sound ended and the channel went to another one.
template<class Play>
int playObj(lua_State *L, Play play)
{
    auto *inst = static_cast<SfxInstance*>(lua_newuserdata(L, sizeof(SfxInstance)));
    inst->channel = -1;
    inst->finishCount = 0;
    luaL_getmetatable(L, INSTANCE_MT);
    lua_setmetatable(L, -2);

    Mix_Chunk *chunk = optChunk(L, 1);
    int channel = chunk ? Mix_GroupAvailable(-1) : -1;
    if(channel < 0)
        return 1;

    unsigned finishCount = SoundScript_ChannelFinishCount(channel);
    if(play(channel, chunk) == channel)
    {
        inst->channel = channel;
        inst->finishCount = finishCount;
    }

    return 1;
}

int sfxPlayObj(lua_State *L)
{
    int loops = luaL_checkint(L, 2);
    return playObj(L, [=](int ch, Mix_Chunk *c) { return Mix_PlayChannelVol(ch, c, loops, sfxVolume(MIX_MAX_VOLUME)); });
}

int sfxPlayObjVol(lua_State *L)
{
    int loops = luaL_checkint(L, 2);
    int volume = sfxVolumeArg(L, 3);
    return playObj(L, [=](int ch, Mix_Chunk *c) { return Mix_PlayChannelVol(ch, c, loops, volume); });
}

int sfxPlayObjTimed(lua_State *L)
{
    int loops = luaL_checkint(L, 2);
    int ticks = luaL_checkint(L, 3);
    return playObj(L, [=](int ch, Mix_Chunk *c) { return Mix_PlayChannelTimedVolume(ch, c, loops, ticks, sfxVolume(MIX_MAX_VOLUME)); });
}

int sfxPlayObjTimedVol(lua_State *L)
{
    int loops = luaL_checkint(L, 2);
    int ticks = luaL_checkint(L, 3);
    int volume = sfxVolumeArg(L, 4);
    return playObj(L, [=](int ch, Mix_Chunk *c) { return Mix_PlayChannelTimedVolume(ch, c, loops, ticks, volume); });
}

int sfxFadeInObj(lua_State *L)
{
    int loops = luaL_checkint(L, 2);
    int ms = luaL_checkint(L, 3);
    return playObj(L, [=](int ch, Mix_Chunk *c) { return Mix_FadeInChannelVolume(ch, c, loops, ms, sfxVolume(MIX_MAX_VOLUME)); });
}

int sfxFadeInObjVol(lua_State *L)
{
    int loops = luaL_checkint(L, 2);
    int ms = luaL_checkint(L, 3);
    int volume = sfxVolumeArg(L, 4);
    return playObj(L, [=](int ch, Mix_Chunk *c) { return Mix_FadeInChannelVolume(ch, c, loops, ms, volume); });
}

int sfxFadeInObjTimed(lua_State *L)
{
    int loops = luaL_checkint(L, 2);
    int ms = luaL_checkint(L, 3);
    int ticks = luaL_checkint(L, 4);
    return playObj(L, [=](int ch, Mix_Chunk *c) { return Mix_FadeInChannelTimedVolume(ch, c, loops, ms, ticks, sfxVolume(MIX_MAX_VOLUME)); });
}

int sfxFadeInObjTimedVol(lua_State *L)
{
    int loops = luaL_checkint(L, 2);
    int ms = luaL_checkint(L, 3);
    int ticks = luaL_checkint(L, 4);
    int volume = sfxVolumeArg(L, 5);
    return playObj(L, [=](int ch, Mix_Chunk *c) { return Mix_FadeInChannelTimedVolume(ch, c, loops, ms, ticks, volume); });
}

// The instance's channel while its sound is still the one there, else -1
int liveChannel(lua_State *L)
{
    auto *inst = static_cast<SfxInstance*>(luaL_checkudata(L, 1, INSTANCE_MT));
    if(inst->channel < 0)
        return -1;

    if(SoundScript_ChannelFinishCount(inst->channel) != inst->finishCount)
    {
        inst->channel = -1;
        return -1;
    }

    return inst->channel;
}

int instPause(lua_State *L)
{
    int channel = liveChannel(L);
    if(channel >= 0)
        Mix_Pause(channel);
    return 0;
}

int instResume(lua_State *L)
{
    int channel = liveChannel(L);
    if(channel >= 0)
        Mix_Resume(channel);
    return 0;
}

int instStop(lua_State *L)
{
    int channel = liveChannel(L);
    if(channel >= 0)
        Mix_HaltChannel(channel);
    return 0;
}

int instExpire(lua_State *L)
{
    int channel = liveChannel(L);
    if(channel >= 0)
        Mix_ExpireChannel(channel, luaL_checkint(L, 2));
    return 0;
}

int instFadeOut(lua_State *L)
{
    int channel = liveChannel(L);
    if(channel >= 0)
        Mix_FadeOutChannel(channel, luaL_checkint(L, 2));
    return 0;
}

int instIsPlaying(lua_State *L)
{
    int channel = liveChannel(L);
    lua_pushboolean(L, channel >= 0 && Mix_Playing(channel) != 0);
    return 1;
}

int instIsPaused(lua_State *L)
{
    int channel = liveChannel(L);
    lua_pushboolean(L, channel >= 0 && Mix_Paused(channel) != 0);
    return 1;
}

int instIsFading(lua_State *L)
{
    int channel = liveChannel(L);
    lua_pushboolean(L, channel >= 0 && Mix_FadingChannel(channel) != MIX_NO_FADING);
    return 1;
}

int instVolume(lua_State *L)
{
    int channel = liveChannel(L);
    if(channel >= 0)
        Mix_Volume(channel, sfxVolume(luaL_checkint(L, 2)));
    return 0;
}

int instSetPanning(lua_State *L)
{
    int channel = liveChannel(L);
    if(channel >= 0)
        Mix_SetPanning(channel, (Uint8)luaL_checkint(L, 2), (Uint8)luaL_checkint(L, 3));
    return 0;
}

int instSetDistance(lua_State *L)
{
    int channel = liveChannel(L);
    if(channel >= 0)
        Mix_SetDistance(channel, (Uint8)luaL_checkint(L, 2));
    return 0;
}

int instSet3DPosition(lua_State *L)
{
    int channel = liveChannel(L);
    if(channel >= 0)
        Mix_SetPosition(channel, (Sint16)luaL_checkint(L, 2), (Uint8)luaL_checkint(L, 3));
    return 0;
}

int instSetReverseStereo(lua_State *L)
{
    int channel = liveChannel(L);
    if(channel >= 0)
        Mix_SetReverseStereo(channel, lua_toboolean(L, 2) ? 1 : 0);
    return 0;
}


// ---- Music ----

void updateMusicClock()
{
    Uint32 now = SDL_GetTicks();
    Mix_Music *mus = SoundScript_Music();
    if(mus && Mix_PlayingMusicStream(mus) && !Mix_PausedMusicStream(mus))
        s_musicClock += (now - s_musicClockTicks) / 1000.0;
    s_musicClockTicks = now;
}

int musicOpen(lua_State *L)
{
    SoundScript_OpenMusic(luaL_checkstring(L, 1));
    return 0;
}

void playMusic(int fadeInMs)
{
    Mix_Music *mus = SoundScript_Music();
    if(mus && !Mix_PlayingMusicStream(mus))
    {
        s_musicClock = 0.0;
        s_musicClockTicks = SDL_GetTicks();
    }
    SoundScript_PlayMusic(fadeInMs);
}

int musicPlay(lua_State *)
{
    playMusic(0);
    return 0;
}

int musicPlayFadeIn(lua_State *L)
{
    playMusic(luaL_checkint(L, 1));
    return 0;
}

int musicStop(lua_State *)
{
    updateMusicClock();
    SoundScript_StopMusic(0);
    return 0;
}

int musicStopFadeOut(lua_State *L)
{
    SoundScript_StopMusic(luaL_checkint(L, 1));
    return 0;
}

int musicPause(lua_State *)
{
    updateMusicClock();
    if(SoundScript_Music())
        Mix_PauseMusicStream(SoundScript_Music());
    return 0;
}

int musicResume(lua_State *)
{
    updateMusicClock();
    SoundScript_PlayMusic(0);
    return 0;
}

int musicIsPlaying(lua_State *L)
{
    Mix_Music *mus = SoundScript_Music();
    lua_pushboolean(L, mus && Mix_PlayingMusicStream(mus));
    return 1;
}

int musicIsPaused(lua_State *L)
{
    Mix_Music *mus = SoundScript_Music();
    lua_pushboolean(L, mus && Mix_PausedMusicStream(mus));
    return 1;
}

int musicIsFading(lua_State *L)
{
    Mix_Music *mus = SoundScript_Music();
    lua_pushboolean(L, mus && Mix_FadingMusicStream(mus) != MIX_NO_FADING);
    return 1;
}

int getMusicVolume(lua_State *L)
{
    lua_pushinteger(L, SoundScript_GetMusicVolume());
    return 1;
}

int musicVolume(lua_State *L)
{
    SoundScript_SetMusicVolume(luaL_checkint(L, 1));
    return 0;
}

template<const char *(*Tag)(const Mix_Music *)>
int musicTag(lua_State *L)
{
    Mix_Music *mus = SoundScript_Music();
    const char *tag = mus ? Tag(mus) : nullptr;
    lua_pushstring(L, tag ? tag : "");
    return 1;
}

int musicSetPos(lua_State *L)
{
    Mix_Music *mus = SoundScript_Music();
    if(mus)
        Mix_SetMusicPositionStream(mus, luaL_checknumber(L, 1));
    return 0;
}

// Getters that take the music stream and return a number
template<double (*Get)(Mix_Music *)>
int musicNumber(lua_State *L)
{
    Mix_Music *mus = SoundScript_Music();
    lua_pushnumber(L, mus ? Get(mus) : -1.0);
    return 1;
}

double musicTracks(Mix_Music *mus)
{
    return Mix_GetMusicTracks(mus);
}

template<int (*Set)(Mix_Music *, double)>
int musicSetNumber(lua_State *L)
{
    Mix_Music *mus = SoundScript_Music();
    if(mus)
        Set(mus, luaL_checknumber(L, 1));
    return 0;
}

int musicRewind(lua_State *)
{
    Mix_Music *mus = SoundScript_Music();
    if(mus)
        Mix_RewindMusicStream(mus);
    return 0;
}

int musicInstChannelMute(lua_State *L)
{
    Mix_Music *mus = SoundScript_Music();
    if(mus)
        Mix_SetMusicTrackMute(mus, luaL_checkint(L, 1), 1);
    return 0;
}

int musicInstChannelUnmute(lua_State *L)
{
    Mix_Music *mus = SoundScript_Music();
    if(mus)
        Mix_SetMusicTrackMute(mus, luaL_checkint(L, 1), 0);
    return 0;
}

int seizeStream(lua_State *L)
{
    SoundScript_SeizeMusic(luaL_checkint(L, 1), true);
    return 0;
}

int releaseStream(lua_State *L)
{
    SoundScript_SeizeMusic(luaL_checkint(L, 1), false);
    return 0;
}

int resetMciSections(lua_State *)
{
    SoundScript_SeizeMusic(-1, false);
    return 0;
}

// The section's music changes; the playing music follows when it's player 1's
// section (and the engine still owns that section's music)
void changeMusic(int section, int musicId, const std::string &path, int fadeMs)
{
    if(section < 0 || section > maxSections)
        return;

    bool changed;
    if(musicId == 24)
    {
        changed = bgMusic[section] != 24 || CustomMusic[section] != path;
        CustomMusic[section] = path;
    }
    else
        changed = bgMusic[section] != musicId;

    bgMusic[section] = musicId;

    if(!changed || curMusic < 0 || section != Player[1].Section)
        return;

    if(musicId == 0 && fadeMs > 0)
        FadeOutMusic(fadeMs);
    else
        StartMusic(section, fadeMs > 0 ? fadeMs : 0);
}

int musicChange(lua_State *L)
{
    int section = luaL_checkint(L, 1);
    int fadeMs = luaL_optint(L, 3, -1);

    if(lua_type(L, 2) == LUA_TSTRING && lua_objlen(L, 2) > 0)
        changeMusic(section, 24, lua_tostring(L, 2), fadeMs);
    else if(lua_type(L, 2) == LUA_TSTRING)
        changeMusic(section, 0, std::string(), fadeMs);
    else
    {
        int musicId = luaL_checkint(L, 2);
        changeMusic(section, musicId < 0 ? 0 : musicId, std::string(), fadeMs);
    }

    return 0;
}

int musicFadeOut(lua_State *L)
{
    changeMusic(luaL_checkint(L, 1), 0, std::string(), luaL_checkint(L, 2));
    return 0;
}

int audioClock(lua_State *L)
{
    lua_pushnumber(L, SDL_GetTicks() / 1000.0);
    return 1;
}

int musicClock(lua_State *L)
{
    updateMusicClock();
    lua_pushnumber(L, s_musicClock);
    return 1;
}


// ---- SMBX's own sounds (SMBX2's Audio.sounds[A]) ----

int setOverrideForAlias(lua_State *L)
{
    SoundScript_SetSfxOverride(aliasSound(L, 1), optChunk(L, 2));
    return 0;
}

int getChunkForAlias(lua_State *L)
{
    pushChunk(L, SoundScript_GetSfxChunk(aliasSound(L, 1)));
    return 1;
}

int setMuteForAlias(lua_State *L)
{
    SoundScript_SetSfxMuted(aliasSound(L, 1), lua_toboolean(L, 2) != 0);
    return 0;
}

int getMuteForAlias(lua_State *L)
{
    lua_pushboolean(L, SoundScript_GetSfxMuted(aliasSound(L, 1)));
    return 1;
}


const luaL_Reg s_audioFuncs[] =
{
    {"MusicOpen", &musicOpen},
    {"MusicPlay", &musicPlay},
    {"MusicPlayFadeIn", &musicPlayFadeIn},
    {"MusicStop", &musicStop},
    {"MusicStopFadeOut", &musicStopFadeOut},
    {"MusicPause", &musicPause},
    {"MusicResume", &musicResume},
    {"MusicIsPlaying", &musicIsPlaying},
    {"MusicIsPaused", &musicIsPaused},
    {"MusicIsFading", &musicIsFading},
    {"_GetMusicVolume", &getMusicVolume},
    {"MusicVolume", &musicVolume},
    {"MusicTitle", &musicTag<Mix_GetMusicTitle>},
    {"MusicTitleTag", &musicTag<Mix_GetMusicTitleTag>},
    {"MusicArtistTag", &musicTag<Mix_GetMusicArtistTag>},
    {"MusicAlbumTag", &musicTag<Mix_GetMusicAlbumTag>},
    {"MusicCopyrightTag", &musicTag<Mix_GetMusicCopyrightTag>},
    {"MusicSetPos", &musicSetPos},
    {"MusicGetPos", &musicNumber<Mix_GetMusicPosition>},
    {"MusicGetLoopStart", &musicNumber<Mix_GetMusicLoopStartTime>},
    {"MusicGetLoopEnd", &musicNumber<Mix_GetMusicLoopEndTime>},
    {"MusicGetLoopLength", &musicNumber<Mix_GetMusicLoopLengthTime>},
    {"SeizeStream", &seizeStream},
    {"ReleaseStream", &releaseStream},
    {"resetMciSections", &resetMciSections},
    {"MusicChange", &musicChange},
    {"MusicFadeOut", &musicFadeOut},
    {"MusicRewind", &musicRewind},
    {"MusicGetInstChannelCount", &musicNumber<musicTracks>},
    {"MusicInstChannelMute", &musicInstChannelMute},
    {"MusicInstChannelUnmute", &musicInstChannelUnmute},
    {"MusicSetTempo", &musicSetNumber<Mix_SetMusicTempo>},
    {"MusicSetPitch", &musicSetNumber<Mix_SetMusicPitch>},
    {"MusicSetSpeed", &musicSetNumber<Mix_SetMusicSpeed>},
    {"MusicGetTempo", &musicNumber<Mix_GetMusicTempo>},
    {"MusicGetPitch", &musicNumber<Mix_GetMusicPitch>},
    {"MusicGetSpeed", &musicNumber<Mix_GetMusicSpeed>},

    {"newMix_Chunk", &newMixChunk},
    {"clearSFXBuffer", &clearSFXBuffer},
    {"playSFX", &playSFX},
    {"SfxOpen", &sfxOpen},
    {"SfxPlayCh", &sfxPlayCh},
    {"SfxPlayChVol", &sfxPlayChVol},
    {"SfxPlayChTimed", &sfxPlayChTimed},
    {"SfxPlayChTimedVol", &sfxPlayChTimedVol},
    {"SfxFadeInCh", &sfxFadeInCh},
    {"SfxFadeInChVol", &sfxFadeInChVol},
    {"SfxFadeInChTimed", &sfxFadeInChTimed},
    {"SfxFadeInChTimedVol", &sfxFadeInChTimedVol},
    {"SfxPause", &sfxPause},
    {"SfxResume", &sfxResume},
    {"SfxStop", &sfxStop},
    {"SfxExpire", &sfxExpire},
    {"SfxFadeOut", &sfxFadeOut},
    {"SfxIsPlaying", &sfxIsPlaying},
    {"SfxIsPaused", &sfxIsPaused},
    {"SfxIsFading", &sfxIsFading},
    {"SfxVolume", &sfxChannelVolume},
    {"SfxSetPanning", &sfxSetPanning},
    {"SfxSetDistance", &sfxSetDistance},
    {"SfxSet3DPosition", &sfxSet3DPosition},
    {"SfxReverseStereo", &sfxReverseStereo},
    {"MixedSfxVolume", &mixedSfxVolume},

    {"SfxPlayObj", &sfxPlayObj},
    {"SfxPlayObjVol", &sfxPlayObjVol},
    {"SfxPlayObjTimed", &sfxPlayObjTimed},
    {"SfxPlayObjTimedVol", &sfxPlayObjTimedVol},
    {"SfxFadeInObj", &sfxFadeInObj},
    {"SfxFadeInObjVol", &sfxFadeInObjVol},
    {"SfxFadeInObjTimed", &sfxFadeInObjTimed},
    {"SfxFadeInObjTimedVol", &sfxFadeInObjTimedVol},

    {"AudioClock", &audioClock},
    {"MusicClock", &musicClock},

    {"__setOverrideForAlias", &setOverrideForAlias},
    {"__getChunkForAlias", &getChunkForAlias},
    {"__setMuteForAlias", &setMuteForAlias},
    {"__getMuteForAlias", &getMuteForAlias},
    {nullptr, nullptr}
};

const luaL_Reg s_instanceMethods[] =
{
    {"Pause", &instPause},
    {"Resume", &instResume},
    {"Stop", &instStop},
    {"Expire", &instExpire},
    {"FadeOut", &instFadeOut},
    {"IsPlaying", &instIsPlaying},
    {"IsPaused", &instIsPaused},
    {"IsFading", &instIsFading},
    {"Volume", &instVolume},
    {"SetPanning", &instSetPanning},
    {"SetDistance", &instSetDistance},
    {"Set3DPosition", &instSet3DPosition},
    {"SetReverseStereo", &instSetReverseStereo},
    {nullptr, nullptr}
};

} // namespace

size_t xtech_lua_audioChunkCount()
{
    return s_chunks.size();
}

void xtech_lua_register_audio(lua_State *L)
{
    luaL_newmetatable(L, CHUNK_MT);
    lua_pushcfunction(L, &chunkIndex);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, &chunkNewIndex);
    lua_setfield(L, -2, "__newindex");
    lua_pop(L, 1);

    luaL_newmetatable(L, INSTANCE_MT);
    lua_newtable(L);
    luaL_register(L, nullptr, s_instanceMethods);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, CHUNK_VIEWS);

    // One userdata owns the opened chunks; it's collected when the state closes
    lua_newuserdata(L, 1);
    luaL_newmetatable(L, OWNER_MT);
    lua_pushcfunction(L, &ownerGc);
    lua_setfield(L, -2, "__gc");
    lua_setmetatable(L, -2);
    lua_setfield(L, LUA_REGISTRYINDEX, OWNER_MT);

    lua_newtable(L);
    luaL_register(L, nullptr, s_audioFuncs);
    lua_setglobal(L, "xtech_lunaAudio");
}
