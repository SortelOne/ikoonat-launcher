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

#include <cstring>
#include <string>
#include <list>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <chrono>
#include <algorithm> // smbx2compat: std::remove

extern "C"
{
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

#include <luabind/luabind.hpp>
#include <luabind/class.hpp>
#include <luabind/function.hpp>
#include <luabind/object.hpp>

#include <Logger/logger.h>
#include <Utils/files.h>
#include <fmt_format_ne.h>

#include "globals.h"
#include "luabind_num_t.h" // smbx2compat: num_t <-> Lua number
#include "global_dirs.h"
#include "global_strings.h"
#include "location.h"
#include "layers.h"
#include "player.h"
#include "npc.h"
#include "graphics.h"
#include "sound.h"
#include "collision.h"
#include "config.h"
#include "core/msgbox.h"
#include "game_main.h"
#include "npc_traits.h"
#include "npc_constant_traits.h" // smbx2compat: mem() NPC type arrays
#include "screen.h" // smbx2compat: mem() camera arrays
#include "effect.h"
#include "main/cheat_code.h"
#include "main/level_medals.h"
#include "main/level_save_info.h"
#include "main/level_file.h"
#include "main/trees.h" // smbx2compat: block quadtree queries
#include "main/smbx2_collision.h" // smbx2compat: collision groups

#include "script/luna/lunaplayer.h"
#include "script/luna/lunanpc.h"
#include "script/luna/lunalayer.h"
#include "script/luna/lunablock.h"
#include "script/luna/lunadefs.h"
#include "script/luna/mememu.h" // smbx2compat: mem() for SMBX2 scripts
#include <Utils/dir_list_ci.h> // smbx2compat: case-insensitive lookups
#include <Graphics/graphics_funcs.h> // smbx2compat: image sizes and pixels
#include <FreeImageLite.h> // smbx2compat
#include <DirManager/dirman.h> // smbx2compat: Misc.listFiles
#include "script/luna/autocode_manager.h"
#include "script/luna/lunarender.h"
#include "core/render.h"
#include "script/luna/lunaspriteman.h"
#include "script/luna/lunaimgbox.h"
#include "script/luna/csprite.h"
#include "script/luna/sprite_component.h"
#include "script/luna/sprite_funcs.h"
#include "script/luna/renderop_string.h"
#include "script/luna/renderop_bitmap.h"
#include "script/luna/lunarender.h"
#include "core/render.h"

#include "xtech_lua_bindings.h"
#include "xtech_lua_main.h"
#include "xtech_lua_events.h"

// Forward declaration of event subscribe function (defined in xtech_lua_events.cpp)
extern "C" int xtech_lua_subscribe_event(lua_State *L);


// ============================================================================
// Helper functions
// ============================================================================

static inline int s_round2int(num_t d)
{
    return (int)num_t::floor(d + 0.5_n);
}

static FIELDTYPE StrToFieldtype(const std::string &string)
{
    if(string == "b")
        return FT_BYTE;
    else if(string == "s" || string == "w")
        return FT_WORD;
    else if(string == "dw")
        return FT_DWORD;
    else if(string == "f")
        return FT_FLOAT;
    else if(string == "df")
        return FT_DFLOAT;
    return FT_BYTE;
}

// ============================================================================
// NPC bitfield accessor wrappers (for bitfield members that luabind can't
// directly bind with def_readwrite)
// ============================================================================

namespace LuaNPCExt
{

// Generator bitfield
static bool npc_getGenerator(NPC_t *n) { return n ? n->Generator : false; }
static void npc_setGenerator(NPC_t *n, bool v) { if(n) n->Generator = v; }

static bool npc_getGeneratorActive(NPC_t *n) { return n ? n->GeneratorActive : false; }
static void npc_setGeneratorActive(NPC_t *n, bool v) { if(n) n->GeneratorActive = v; }

static bool npc_getChat(NPC_t *n) { return n ? n->Chat : false; }
static void npc_setChat(NPC_t *n, bool v) { if(n) n->Chat = v; }

static bool npc_getLegacy(NPC_t *n) { return n ? n->Legacy : false; }
static void npc_setLegacy(NPC_t *n, bool v) { if(n) n->Legacy = v; }

static bool npc_getTurnAround(NPC_t *n) { return n ? n->TurnAround : false; }
static void npc_setTurnAround(NPC_t *n, bool v) { if(n) n->TurnAround = v; }

static bool npc_getTurnBackWipe(NPC_t *n) { return n ? n->TurnBackWipe : false; }
static void npc_setTurnBackWipe(NPC_t *n, bool v) { if(n) n->TurnBackWipe = v; }

static bool npc_getplayerTemp(NPC_t *n) { return n ? n->playerTemp : false; }
static void npc_setplayerTemp(NPC_t *n, bool v) { if(n) n->playerTemp = v; }

static bool npc_getNoLavaSplash(NPC_t *n) { return n ? n->NoLavaSplash : false; }
static void npc_setNoLavaSplash(NPC_t *n, bool v) { if(n) n->NoLavaSplash = v; }

static bool npc_getBouce(NPC_t *n) { return n ? n->Bouce : false; }
static void npc_setBouce(NPC_t *n, bool v) { if(n) n->Bouce = v; }

static bool npc_getDefaultStuck(NPC_t *n) { return n ? n->DefaultStuck : false; }
static void npc_setDefaultStuck(NPC_t *n, bool v) { if(n) n->DefaultStuck = v; }

static bool npc_getRespawnDelay(NPC_t *n) { return n ? n->RespawnDelay : false; }
static void npc_setRespawnDelay(NPC_t *n, bool v) { if(n) n->RespawnDelay = v; }

// Generator field accessors (stored in Special3/Special4/Special5)
static int npc_getGeneratorDirection(NPC_t *n) { return n ? n->GeneratorDirection() : 0; }
static int npc_getGeneratorEffect(NPC_t *n) { return n ? n->GeneratorEffect() : 0; }
static int npc_getGeneratorTimeMax(NPC_t *n) { return n ? (int)n->GeneratorTimeMax() : 0; }
static void npc_setGeneratorTimeMax(NPC_t *n, int v) { if(n) n->GeneratorTimeMax() = v; }
static int npc_getGeneratorTime(NPC_t *n) { return n ? (int)n->GeneratorTime() : 0; }
static void npc_setGeneratorTime(NPC_t *n, int v) { if(n) n->GeneratorTime() = v; }

static bool npc_getStuck(NPC_t *n) { return n ? n->Stuck : false; }
static void npc_setStuck(NPC_t *n, bool v) { if(n) n->Stuck = v; }

static bool npc_getShadow(NPC_t *n) { return n ? n->Shadow : false; }
static void npc_setShadow(NPC_t *n, bool v) { if(n) n->Shadow = v; }

static int npc_getQuicksand(NPC_t *n) { return n ? (int)n->Quicksand : 0; }
static void npc_setQuicksand(NPC_t *n, int v) { if(n) n->Quicksand = (uint8_t)v; }

// getPermID: returns the 1-based array index of this NPC
static int npc_getPermID(NPC_t *n)
{
    if(!n) return -1;
    ptrdiff_t idx = n - &NPC[0];
    if(idx >= 0 && idx < numNPCs)
        return (int)(idx + 1); // 1-based
    return -1;
}

// Name getter/setter using string bank
static std::string npc_getName(NPC_t *n) {
    if(!n || n->Name == STRINGINDEX_NONE) return "";
    return GetS(n->Name);
}
static void npc_setName(NPC_t *n, const std::string &v) {
    if(n) SetS(n->Name, v);
}

} // namespace LuaNPCExt


// ============================================================================
// Player bitfield accessor wrappers
// ============================================================================

namespace LuaPlayerExt
{

static bool player_getGroundPound(Player_t *p) { return p ? p->GroundPound : false; }
static void player_setGroundPound(Player_t *p, bool v) { if(p) p->GroundPound = v; }

static bool player_getGroundPound2(Player_t *p) { return p ? p->GroundPound2 : false; }
static void player_setGroundPound2(Player_t *p, bool v) { if(p) p->GroundPound2 = v; }

static bool player_getCanPound(Player_t *p) { return p ? p->CanPound : false; }
static void player_setCanPound(Player_t *p, bool v) { if(p) p->CanPound = v; }

static bool player_getAltRunRelease(Player_t *p) { return p ? p->AltRunRelease : false; }
static void player_setAltRunRelease(Player_t *p, bool v) { if(p) p->AltRunRelease = v; }

static bool player_getDuckRelease(Player_t *p) { return p ? p->DuckRelease : false; }
static void player_setDuckRelease(Player_t *p, bool v) { if(p) p->DuckRelease = v; }

static bool player_getSlippyWall(Player_t *p) { return p ? p->SlippyWall : false; }
static void player_setSlippyWall(Player_t *p, bool v) { if(p) p->SlippyWall = v; }

static bool player_getJumpOffWall(Player_t *p) { return p ? p->JumpOffWall : false; }
static void player_setJumpOffWall(Player_t *p, bool v) { if(p) p->JumpOffWall = v; }

} // namespace LuaPlayerExt


// ============================================================================
// Player API wrappers
// ============================================================================

namespace LuaPlayer
{

static Player_t* get(int num)
{
    return PlayerF::Get(num);
}

static int count()
{
    return numPlayers;
}

// smbx2compat: LunaLua's Misc._playerHarm / _playerKill (SMBX's own routines)
static void harm(int index)
{
    if(index >= 1 && index <= numPlayers)
        PlayerHurt(index);
}

static void kill(int index)
{
    if(index >= 1 && index <= numPlayers)
        KillPlayer(index);
}

static void filterToBig(Player_t *p)
{
    if(p) PlayerF::FilterToBig(p);
}

static void filterToSmall(Player_t *p)
{
    if(p) PlayerF::FilterToSmall(p);
}

static void filterToFire(Player_t *p)
{
    if(p) PlayerF::FilterToFire(p);
}

static void filterMount(Player_t *p)
{
    if(p) PlayerF::FilterMount(p);
}

static void filterReservePowerup(Player_t *p)
{
    if(p) PlayerF::FilterReservePowerup(p);
}

static void cycleRight(Player_t *p)
{
    if(p) PlayerF::CycleRight(p);
}

static void cycleLeft(Player_t *p)
{
    if(p) PlayerF::CycleLeft(p);
}

static void infiniteFlying(int player)
{
    PlayerF::InfiniteFlying(player);
}

static bool pressingUp(Player_t *p)
{
    return p ? PlayerF::PressingUp(p) : false;
}

static bool pressingDown(Player_t *p)
{
    return p ? PlayerF::PressingDown(p) : false;
}

static bool pressingLeft(Player_t *p)
{
    return p ? PlayerF::PressingLeft(p) : false;
}

static bool pressingRight(Player_t *p)
{
    return p ? PlayerF::PressingRight(p) : false;
}

static bool pressingJump(Player_t *p)
{
    return p ? PlayerF::PressingJump(p) : false;
}

static bool pressingRun(Player_t *p)
{
    return p ? PlayerF::PressingRun(p) : false;
}

static bool pressingSEL(Player_t *p)
{
    return p ? PlayerF::PressingSEL(p) : false;
}

static bool isHoldingSpriteType(Player_t *p, int npcId)
{
    return p ? PlayerF::IsHoldingSpriteType(p, npcId) : false;
}

static bool usesHearts(Player_t *p)
{
    return p ? PlayerF::UsesHearts(p) : false;
}

static void memSet(size_t offset, num_t value, int op, int ftype)
{
    PlayerF::MemSet(offset, value, (OPTYPE)op, (FIELDTYPE)ftype);
}

// P6: expose PlayerDismount — silent dismount (mount becomes NPC, no jump/sound)
static void dismount(Player_t *p)
{
    if(!p) return;
    int idx = (int)(p - &Player[1]) + 1; // 1-based player index
    if(idx >= 1 && idx <= numPlayers)
        ::PlayerDismount(idx, false);  // silent: no jump velocity, no sound
}

} // namespace LuaPlayer


// ============================================================================
// NPC API wrappers
// ============================================================================

namespace LuaNPC
{

static NPC_t* get(int index)
{
    return NpcF::Get(index);
}

static int count()
{
    return numNPCs;
}

static NPC_t* getFirstMatch(int id, int section)
{
    return NpcF::GetFirstMatch(id, section);
}

static void memSet(int id, size_t offset, num_t value, int op, int ftype)
{
    NpcF::MemSet(id, offset, value, (OPTYPE)op, (FIELDTYPE)ftype);
}

static void allSetHits(int identity, int section, int hits)
{
    NpcF::AllSetHits(identity, section, hits);
}

static void kill(int a, int b)
{
    KillNPC(a, b);
}

static void hurt(int a, int b, int c)
{
    NPCHit(a, b, c);
}

static NPC_t* getByPermID(int permId)
{
    if(permId >= 1 && permId <= numNPCs)
        return const_cast<NPC_t*>(&NPC[permId]);
    return nullptr;
}

static void forEach(int id, int section, luabind::object callback)
{
    if(!callback.is_valid()) return;

    lua_State* L = xtech_lua_getState();

    for(int i = 1; i <= numNPCs; i++)
    {
        NPC_t *npc = const_cast<NPC_t*>(&NPC[i]);
        if(!npc->Active) continue;
        if(id != 0 && npc->Type != (NPCID)id) continue;
        if(section != 0 && npc->Section != section) continue;

        int top = lua_gettop(L);

        try
        {
            callback(npc);
        }
        catch(const luabind::error &e)
        {
            // smbx2compat: luabind::error already popped the message
            lua_settop(L, top);
            pLogWarning("Lua error in NPC forEach callback: %s", e.what());
        }
        catch(const std::exception &e)
        {
            lua_settop(L, top);
            pLogWarning("Lua exception in NPC forEach callback: %s", e.what());
        }
    }
}

// P3: NCreate - dynamically spawn an NPC
static NPC_t* lua_npc_create(int type, num_t x, num_t y, num_t xspd, num_t yspd)
{
    if(numNPCs >= maxNPCs)
        return nullptr;

    numNPCs++;
    auto &n = NPC[numNPCs];
    n.Type = (NPCID)type;
    n.Location.X = x;
    n.Location.Y = y;
    n.Location.Width = NPCWidth(type);
    n.Location.Height = NPCHeight(type);
    n.Location.SpeedX = xspd;
    n.Location.SpeedY = yspd;
    n.Active = true;
    n.TimeLeft = 100;
    n.Direction = (xspd >= 0 ? 1 : -1);
    n.Frame = 0;
    n.FrameCount = 0;
    n.Reset.fill(false);

    syncLayers_NPC(numNPCs);
    CheckSectionNPC(numNPCs);

    return const_cast<NPC_t*>(&NPC[numNPCs]);
}

// smbx2compat: LunaLua's NPC.spawn (LuaProxyNPC.cpp), which SMBX2's ffi_npc.lua
// builds on: a blank NPC at (x, y), active, in the given section. Returns the
// new NPC's 0-based index (LunaLua's), or -1 when the NPC array is full.
static int lua_npc_spawnLegacy(int type, double x, double y, int section, bool respawn, bool centered)
{
    if(numNPCs >= maxNPCs || type < 1 || type > maxNPCType)
        return -1;

    numNPCs++;
    auto &n = NPC[numNPCs];
    n = NPC_t();
    n.Type = (NPCID)type;
    num_t width = NPCWidth(type), height = NPCHeight(type);
    if(centered)
    {
        x -= 0.5 * (double)width;
        y -= 0.5 * (double)height;
    }
    n.Location.X = num_t(x, nullptr);
    n.Location.Y = num_t(y, nullptr);
    n.Location.Width = width;
    n.Location.Height = height;
    n.DefaultLocationX = n.Location.X;
    n.DefaultLocationY = n.Location.Y;
    if(respawn)
        n.DefaultType = (NPCID)type;
    n.TimeLeft = 180;
    n.Active = true;
    n.Section = section;

    syncLayers_NPC(numNPCs);

    return numNPCs - 1;
}

// getByName
static NPC_t* npc_getByName(const std::string &name) {
    for(int i = 1; i <= numNPCs; i++)
        if(NPC[i].Name != STRINGINDEX_NONE && GetS(NPC[i].Name) == name)
            return const_cast<NPC_t*>(&NPC[i]);
    return nullptr;
}

} // namespace LuaNPC


// ============================================================================
// Layer API wrappers
// ============================================================================

namespace LuaLayer
{

static Layer_t* get(int index)
{
    return LayerF::Get(index);
}

static int count()
{
    return numLayers;
}

// smbx2compat: LunaLua's LunaLuaShowLayer/LunaLuaHideLayer (index from 0, as in SMBX)
static void show(int index, bool noSmoke)
{
    if(index >= 0 && index < numLayers)
        ShowLayer(index, noSmoke);
}

static void hide(int index, bool noSmoke)
{
    if(index >= 0 && index < numLayers)
        HideLayer(index, noSmoke);
}

static void setXSpeed(Layer_t *layer, num_t speed)
{
    if(layer) LayerF::SetXSpeed(layer, speed);
}

static void setYSpeed(Layer_t *layer, num_t speed)
{
    if(layer) LayerF::SetYSpeed(layer, speed);
}

static void stop(Layer_t *layer)
{
    if(layer) LayerF::Stop(layer);
}

} // namespace LuaLayer


// ============================================================================
// Block API wrappers
// ============================================================================

namespace LuaBlock
{

static Block_t* get(int index)
{
    return BlocksF::Get(index);
}

static int count()
{
    return numBlock;
}

static void setAll(int type1, int type2)
{
    BlocksF::SetAll(type1, type2);
}

static void swapAll(int type1, int type2)
{
    BlocksF::SwapAll(type1, type2);
}

static void showAll(int type)
{
    BlocksF::ShowAll(type);
}

static void hideAll(int type)
{
    BlocksF::HideAll(type);
}

static bool isPlayerTouchingType(int blockType, int collision, Player_t *player)
{
    if(!player) return false;
    return BlocksF::IsPlayerTouchingType(blockType, collision, player);
}

static Block_t* getByPermID(int permId)
{
    if(permId >= 1 && permId <= numBlock)
        return BlocksF::Get(permId);
    return nullptr;
}
static Block_t* getByPermID_wrapper(int permId) { return getByPermID(permId); }

static void forEach(int type, luabind::object callback)
{
    if(!callback.is_valid()) return;

    for(int i = 1; i <= numBlock; i++)
    {
        Block_t *block = const_cast<Block_t*>(&Block[i]);
        if(type != 0 && block->Type != type) continue;

        try
        {
            callback(block);
        }
        catch(const luabind::error &e)
        {
            // smbx2compat: luabind::error already popped the message
            pLogWarning("Lua error in Block forEach callback: %s", e.what());
        }
        catch(const std::exception &e)
        {
            pLogWarning("Lua exception in Block forEach callback: %s", e.what());
        }
    }
}

// getPermID: returns the 1-based array index of this Block
static int block_getPermID(Block_t *b)
{
    if(!b) return -1;
    ptrdiff_t idx = b - &Block[0];
    if(idx >= 0 && idx < numBlock)
        return (int)(idx + 1); // 1-based
    return -1;
}

// Name getter/setter using string bank
static std::string block_getName(Block_t *b) {
    if(!b || b->Name == STRINGINDEX_NONE) return "";
    return GetS(b->Name);
}
static void block_setName(Block_t *b, const std::string &v) {
    if(b) SetS(b->Name, v);
}

// getByName
static Block_t* block_getByName(const std::string &name) {
    for(int i = 1; i <= numBlock; i++)
        if(Block[i].Name != STRINGINDEX_NONE && GetS(Block[i].Name) == name)
            return const_cast<Block_t*>(&Block[i]);
    return nullptr;
}

} // namespace LuaBlock


// ============================================================================
// Audio API wrappers
// ============================================================================

namespace LuaAudio
{

// Helper: resolve audio file path (custom dir first, then episode dir)
static std::string resolveAudioPath(const std::string &filename)
{
    if(filename.empty()) return {};
    // smbx2compat: SMBX2 passes absolute paths (its own sounds live outside the episode)
    if(Files::isAbsolute(filename))
        return Files::fileExists(filename) ? filename : std::string();
    std::string path = g_dirCustom.resolveFileCaseExistsAbs(filename);
    if(!path.empty()) return path;
    return g_dirEpisode.resolveFileCaseExistsAbs(filename);
}

// ============================================================================
// SFX — built-in sounds
// ============================================================================

static void playSFX(int index, int loops, int volume)
{
    if(index > 0)
        PlaySound(index, loops, volume);
}

static void stopSFX(int index)
{
    if(index > 0)
        StopSfx(index);
}

static void playSFXSpatial(int index, int x, int y, int loops, int volume)
{
    if(index > 0)
        PlaySoundSpatial(index, x, y, x, y, loops, volume);
}

// ============================================================================
// SFX — external custom files
// ============================================================================

static void playSFXExt(const std::string &filename, int loops, int volume)
{
    std::string full_path = resolveAudioPath(filename);
    if(!full_path.empty())
        PlayExtSound(full_path, loops, volume);
}

static void stopSFXExt(const std::string &filename)
{
    std::string full_path = resolveAudioPath(filename);
    if(!full_path.empty())
        StopExtSound(full_path);
}

static void preloadSFXExt(const std::string &filename)
{
    std::string full_path = resolveAudioPath(filename);
    if(!full_path.empty())
        PreloadExtSound(full_path);
}

static void stopAllSFX()
{
    StopAllSounds();
}

// ============================================================================
// Music — playback control
// ============================================================================

static void playMusic(int section, int fadeInMs)
{
    StartMusic(section, fadeInMs);
}

static void playMusicFile(const std::string &filename, int fadeInMs)
{
    if(!filename.empty())
        PlayMusic(filename, fadeInMs);
}

static void setMusic(int sec, int musicId, const std::string &filename)
{
    if(sec >= 0 && sec < numSections)
    {
        if(musicId >= 0 && musicId <= 24)
            bgMusic[sec] = musicId;
        if(filename.length() >= 5)
            CustomMusic[sec] = filename;
    }
}

static void stopMusic()
{
    StopMusic();
}

static void pauseMusic()
{
    PauseMusic();
}

static void resumeMusic()
{
    ResumeMusic();
}

static void fadeOutMusic(int ms)
{
    FadeOutMusic(ms);
}

// ============================================================================
// Master volume control
// ============================================================================

static int getSFXVolume()
{
    return (int)g_config.audio_sfx_volume;
}

static void setSFXVolume(int v)
{
    g_config.audio_sfx_volume = v;
    UpdateMusicVolume();
}

static int getMusicVolume()
{
    return (int)g_config.audio_mus_volume;
}

static void setMusicVolume(int v)
{
    g_config.audio_mus_volume = v;
    UpdateMusicVolume();
}

// ============================================================================
// Audio FX — Echo (requires THEXTECH_ENABLE_AUDIO_FX)
// ============================================================================

#ifdef THEXTECH_ENABLE_AUDIO_FX
static void setEcho(luabind::object opts)
{
    SoundFXEchoSetup echo;
    if(opts.is_valid() && luabind::type(opts) == LUA_TTABLE)
    {
        if(luabind::type(opts["on"]) != LUA_TNIL)         echo.echoOn       = luabind::object_cast<int>(opts["on"]);
        if(luabind::type(opts["delay"]) != LUA_TNIL)      echo.echoDelay    = luabind::object_cast<int>(opts["delay"]);
        if(luabind::type(opts["feedback"]) != LUA_TNIL)   echo.echoFeedBack = luabind::object_cast<int>(opts["feedback"]);
        if(luabind::type(opts["mainVolL"]) != LUA_TNIL)   echo.echoMainVolL = luabind::object_cast<int>(opts["mainVolL"]);
        if(luabind::type(opts["mainVolR"]) != LUA_TNIL)   echo.echoMainVolR = luabind::object_cast<int>(opts["mainVolR"]);
        if(luabind::type(opts["echoVolL"]) != LUA_TNIL)   echo.echoVolL     = luabind::object_cast<int>(opts["echoVolL"]);
        if(luabind::type(opts["echoVolR"]) != LUA_TNIL)   echo.echoVolR     = luabind::object_cast<int>(opts["echoVolR"]);
    }
    SoundFX_SetEcho(echo);
}

static void setReverb(luabind::object opts)
{
    SoundFXReverb rv;
    if(opts.is_valid() && luabind::type(opts) == LUA_TTABLE)
    {
        if(luabind::type(opts["mode"]) != LUA_TNIL)       rv.mode      = luabind::object_cast<float>(opts["mode"]);
        if(luabind::type(opts["roomSize"]) != LUA_TNIL)   rv.roomSize  = luabind::object_cast<float>(opts["roomSize"]);
        if(luabind::type(opts["damping"]) != LUA_TNIL)    rv.damping   = luabind::object_cast<float>(opts["damping"]);
        if(luabind::type(opts["wetLevel"]) != LUA_TNIL)   rv.wetLevel  = luabind::object_cast<float>(opts["wetLevel"]);
        if(luabind::type(opts["dryLevel"]) != LUA_TNIL)   rv.dryLevel  = luabind::object_cast<float>(opts["dryLevel"]);
        if(luabind::type(opts["width"]) != LUA_TNIL)      rv.width     = luabind::object_cast<float>(opts["width"]);
    }
    SoundFX_SetReverb(rv);
}

static void clearFX()
{
    SoundFX_Clear();
}
#endif // THEXTECH_ENABLE_AUDIO_FX

} // namespace LuaAudio


// ============================================================================
// HUD / Render API wrappers
// ============================================================================

extern void HudRenderNPC(int npcId, int x, int y, int w, int h);
extern void HudRenderImage(int x, int y, int w, int h, StdPicture &tex, int sx, int sy);

// Simple texture storage for HUD images — uses game's native LoadPicture,
// avoiding LunaImage move-semantics issues.
static std::map<int, StdPicture> s_hudTextures;

namespace LuaHUD
{

// NOTE: parameters use double (not num_t) because luabind cannot convert
// Lua numbers to num_t (num_t's double constructor is explicitly deleted).
static void showText(const std::string &text, double x, double y, double font)
{
    if(text.empty())
        return;
    int iFont = static_cast<int>(font);
    if(iFont < 1 || iFont > 5)
        iFont = 3;

    int ix = static_cast<int>(x);
    int iy = static_cast<int>(y);
    int len = static_cast<int>(text.size());
    SuperPrint(len, text.c_str(), iFont, ix, iy);
}

static void showLevelName(double x, double y, double font)
{
    int iFont = static_cast<int>(font);
    if(iFont < 1 || iFont > 5)
        iFont = 3;
    const std::string &name = LevelName.empty() ? FileName : LevelName;
    if(name.empty())
        return;
    int ix = static_cast<int>(x);
    int iy = static_cast<int>(y);
    int len = static_cast<int>(name.size());
    SuperPrint(len, name.c_str(), iFont, ix, iy);
}

static void showLevelFile(double x, double y, double font)
{
    int iFont = static_cast<int>(font);
    if(iFont < 1 || iFont > 5)
        iFont = 3;
    if(FileName.empty())
        return;
    int ix = static_cast<int>(x);
    int iy = static_cast<int>(y);
    int len = static_cast<int>(FileName.size());
    SuperPrint(len, FileName.c_str(), iFont, ix, iy);
}

static void showImage(double imgResourceCode, double x, double y,
    double sx, double sy, double sw, double sh)
{
    int code = static_cast<int>(imgResourceCode);
    if(code == 0)
        return;

    auto it = s_hudTextures.find(code);
    if(it == s_hudTextures.end() || !it->second.inited)
        return;

    StdPicture &tex = it->second;

    int ix = static_cast<int>(x);
    int iy = static_cast<int>(y);
    int isx = static_cast<int>(sx);
    int isy = static_cast<int>(sy);
    int isw = static_cast<int>(sw);
    int ish = static_cast<int>(sh);

    if(isw <= 0) isw = tex.w;
    if(ish <= 0) ish = tex.h;
    if(isw <= 0 || ish <= 0)
        return;

    HudRenderImage(ix, iy, isw, ish, tex, isx, isy);
}

static void showNPC(double npcId, double x, double y, double w, double h)
{
    HudRenderNPC(static_cast<int>(npcId),
        static_cast<int>(x), static_cast<int>(y),
        static_cast<int>(w), static_cast<int>(h));
}

static void debugPrint(const std::string &text)
{
    pLogDebug("Lua: %s", text.c_str());
}

} // namespace LuaHUD


// ============================================================================
// Variable API wrappers
// ============================================================================

namespace LuaVar
{

static num_t getVar(const std::string &name)
{
    return gAutoMan.GetVar(name);
}

static bool varExists(const std::string &name)
{
    return gAutoMan.VarExists(name);
}

static bool varOperation(const std::string &name, num_t value, int op)
{
    return gAutoMan.VarOperation(name, value, (OPTYPE)op);
}

} // namespace LuaVar


// ============================================================================
// Event API wrappers
// ============================================================================

namespace LuaEvent
{

static void trigger(int section, int eventId)
{
    gAutoMan.ActivateCustomEvents(section, eventId);
}

static void triggerByName(const std::string &eventName)
{
    Autocode *code = gAutoMan.GetEventByRef(eventName);
    if(code)
    {
        code->Activated = true;
        code->Expired = false;
    }
}

// smbx2compat: LunaLua's triggerEvent(name): a level (SMBX) event, not an
// autocode one. LunaLua runs it at once; TriggerEvent queues it for this frame,
// which keeps scripts from re-entering the engine mid-update.
static bool triggerLevelEvent(const std::string &eventName)
{
    eventindex_t index = FindEvent(eventName);
    if(index == EVENT_NONE)
        return false;
    TriggerEvent(index, 0);
    return true;
}

static void cancelByName(const std::string &eventName)
{
    Autocode *code = gAutoMan.GetEventByRef(eventName);
    if(code)
        gAutoMan.DeleteEvent(eventName);
}

} // namespace LuaEvent


// ============================================================================
// BGO (Background Object) API wrappers
// ============================================================================

namespace LuaBGO
{

static Background_t* get(int index)
{
    if(index >= 1 && index <= numBackground)
        return const_cast<Background_t*>(&Background[index]);
    return nullptr;
}

static int count()
{
    return numBackground;
}

static Background_t* getByPermID(int permId)
{
    if(permId >= 1 && permId <= numBackground)
        return const_cast<Background_t*>(&Background[permId]);
    return nullptr;
}

static void forEach(int type, luabind::object callback)
{
    if(!callback.is_valid()) return;

    for(int i = 1; i <= numBackground; i++)
    {
        Background_t *bgo = const_cast<Background_t*>(&Background[i]);
        if(type != 0 && bgo->Type != type) continue;

        try
        {
            callback(bgo);
        }
        catch(const luabind::error &e)
        {
            // smbx2compat: luabind::error already popped the message
            pLogWarning("Lua error in BGO forEach callback: %s", e.what());
        }
        catch(const std::exception &e)
        {
            pLogWarning("Lua exception in BGO forEach callback: %s", e.what());
        }
    }
}

// getPermID: returns the 1-based array index of this BGO
static int bgo_getPermID(Background_t *b)
{
    if(!b) return -1;
    ptrdiff_t idx = b - &Background[1] + 1;
    if(idx >= 0 && idx < numBackground)
        return (int)(idx + 1); // 1-based
    return -1;
}

} // namespace LuaBGO


// ============================================================================
// Water / Liquid API wrappers
// ============================================================================

namespace LuaWater
{

static Water_t* get(int index)
{
    if(index >= 0 && index <= numWater)
        return const_cast<Water_t*>(&Water[index]);
    return nullptr;
}

static int count()
{
    return numWater;
}

static Water_t* getByPermID(int permId)
{
    if(permId >= 0 && permId <= numWater)
        return const_cast<Water_t*>(&Water[permId]);
    return nullptr;
}

// getPermID: returns the 0-based array index of this Water/Liquid
static int water_getPermID(Water_t *w)
{
    if(!w) return -1;
    ptrdiff_t idx = w - &Water[0];
    if(idx >= 0 && idx < numWater)
        return (int)(idx + 0); // 0-based
    return -1;
}

// Name getter/setter using string bank
static std::string water_getName(Water_t *w) {
    if(!w || w->Name == STRINGINDEX_NONE) return "";
    return GetS(w->Name);
}
static void water_setName(Water_t *w, const std::string &v) {
    if(w) SetS(w->Name, v);
}

// getByName
static Water_t* water_getByName(const std::string &name) {
    for(int i = 0; i <= numWater; i++)
        if(Water[i].Name != STRINGINDEX_NONE && GetS(Water[i].Name) == name)
            return const_cast<Water_t*>(&Water[i]);
    return nullptr;
}

} // namespace LuaWater


// ============================================================================
// Warp API wrappers
// ============================================================================

namespace LuaWarp
{

static Warp_t* get(int index)
{
    if(index >= 1 && index <= numWarps)
        return const_cast<Warp_t*>(&Warp[index]);
    return nullptr;
}

static int count()
{
    return numWarps;
}

static Warp_t* getByPermID(int permId)
{
    if(permId >= 1 && permId <= numWarps)
        return const_cast<Warp_t*>(&Warp[permId]);
    return nullptr;
}

// getPermID: returns the 1-based array index of this Warp
static int warp_getPermID(Warp_t *w)
{
    if(!w) return -1;
    ptrdiff_t idx = w - &Warp[1] + 1;
    if(idx >= 0 && idx < numWarps)
        return (int)(idx + 1); // 1-based
    return -1;
}

} // namespace LuaWarp


// ============================================================================
// Section API wrappers
// ============================================================================

namespace LuaSection
{

static SpeedlessLocation_t* get(int index)
{
    if(index >= 0 && index <= numSections)
        return &level[index];
    return nullptr;
}

static int count()
{
    return numSections;
}

static int getBackground(int index)
{
    if(index >= 0 && index <= numSections)
        return Background2[index];
    return 0;
}

static void setBackground(int index, int bgId)
{
    if(index >= 0 && index <= numSections)
        Background2[index] = bgId;
}

static int getMusic(int index)
{
    if(index >= 0 && index <= numSections)
        return bgMusic[index];
    return 0;
}

static void setMusic(int index, int musicId)
{
    if(index >= 0 && index <= numSections)
        bgMusic[index] = musicId;
}

static const std::string& getMusicFile(int index)
{
    static std::string empty;
    if(index >= 0 && index <= numSections)
        return CustomMusic[index];
    return empty;
}

static void setMusicFile(int index, const std::string &filename)
{
    if(index >= 0 && index <= numSections)
        CustomMusic[index] = filename;
}

static bool getOffScreenExit(int index)
{
    if(index >= 0 && index <= numSections)
        return OffScreenExit[index];
    return false;
}

static void setOffScreenExit(int index, bool value)
{
    if(index >= 0 && index <= numSections)
        OffScreenExit[index] = value;
}

} // namespace LuaSection


// ============================================================================
// Effect API wrappers (P3 - FXCreate equivalent)
// ============================================================================

namespace LuaEffect
{

static Effect_t* get(int index)
{
    if(index >= 1 && index <= numEffects)
        return const_cast<Effect_t*>(&Effect[index]);
    return nullptr;
}

static int count()
{
    return numEffects;
}

static Effect_t* create(int effId, num_t x, num_t y, int direction, bool shadow)
{
    Location_t loc;
    loc.X = x;
    loc.Y = y;
    if(NewEffect((EFFID)effId, loc, direction, shadow))
        return const_cast<Effect_t*>(&Effect[numEffects]);
    return nullptr;
}

static void kill(int index)
{
    if(index >= 1 && index <= numEffects)
        KillEffect(index);
}

} // namespace LuaEffect


// ============================================================================
// Misc API wrappers
// ============================================================================

extern std::string s_playerFileName[numCharacters + 1];
extern void LoadCustomPlayer(int character, int state, std::string cFileName);

namespace LuaMisc
{

static int getFrame()
{
    return (int)CommonFrame;
}

static int getSection(Player_t *p)
{
    return p ? (int)p->Section : 0;
}

static void showMsg(const std::string &text)
{
    if(!text.empty())
    {
        MessageText = text;
        g_MessageType = MESSAGE_TYPE_NORMAL;
        PauseGame(PauseCode::Message);
    }
}

static void showMsgInfo(const std::string &text)
{
    if(!text.empty())
    {
        MessageText = text;
        g_MessageType = MESSAGE_TYPE_SYS_INFO;
        PauseGame(PauseCode::Message);
    }
}

static void showMsgWarn(const std::string &text)
{
    if(!text.empty())
    {
        MessageText = text;
        g_MessageType = MESSAGE_TYPE_SYS_WARNING;
        PauseGame(PauseCode::Message);
    }
}

static void cheat(const std::string &code)
{
    if(!code.empty())
    {
        cheats_setBuffer(code, true);
    }
}

static void logMsg(const std::string &msg)
{
    pLogInfo("Lua: %s", msg.c_str());
}

static void logWarn(const std::string &msg)
{
    pLogWarning("Lua: %s", msg.c_str());
}

static void logDebug(const std::string &msg)
{
    pLogDebug("Lua: %s", msg.c_str());
}

// ============================================================================
// Sysval - global game state variables (P3)
// ============================================================================

static int sysval_getLives() { return Lives; }
static void sysval_setLives(int v) { Lives = v; }

static int sysval_getCoins() { return Coins; }
static void sysval_setCoins(int v) { Coins = v; }

// Medal data from CurLevelMedals_t
static int medal_getCount() { return g_curLevelMedals.max; }
static bool medal_isGotten(int variant, bool permanent = true)
{
    int idx = variant - 1;
    if(idx < 0 || idx >= 8) return false;
    if(permanent)
        return ((g_curLevelMedals.prev | g_curLevelMedals.got) >> idx) & 1;
    else
        return ((g_curLevelMedals.got) >> idx) & 1;
}
// World map medal display toggle
static bool misc_getShowMedalsOnWorldMap() { return g_showMedalsOnWorldMap; }
static void misc_setShowMedalsOnWorldMap(bool v) { g_showMedalsOnWorldMap = v; }

static int sysval_getScore() { return Score; }
static void sysval_setScore(int v) { Score = v; }

static int sysval_getScreenX(int screen)
{
    if(screen >= 0 && screen <= 2)
        return (int)vScreen[screen].X;
    return 0;
}

static int sysval_getScreenY(int screen)
{
    if(screen >= 0 && screen <= 2)
        return (int)vScreen[screen].Y;
    return 0;
}

static int sysval_getScreenWidth(int screen)
{
    if(screen >= 0 && screen <= 2)
        return (int)vScreen[screen].Width;
    return 800;
}

static int sysval_getScreenHeight(int screen)
{
    if(screen >= 0 && screen <= 2)
        return (int)vScreen[screen].Height;
    return 600;
}

static int sysval_getScreenTop(int screen)
{
    if(screen >= 0 && screen <= 2)
    {
        int h = (int)vScreen[screen].Height;
        return (h > 600) ? h / 2 - 300 : 0;
    }
    return 0;
}

static int sysval_getScreenLeft(int screen)
{
    if(screen >= 0 && screen <= 2)
        return (int)vScreen[screen].ScreenLeft;
    return 0;
}

static int sysval_getScreenTopDraw(int screen)
{
    if(screen >= 0 && screen <= 2)
        return (int)vScreen[screen].ScreenTop;
    return 0;
}

static int sysval_getScreenCenterX(int screen)
{
    if(screen >= 0 && screen <= 2)
        return (int)vScreen[screen].Width / 2;
    return 400;
}

static bool sysval_getShowHud() { return ShowOnScreenHUD; }
static void sysval_setShowHud(bool v) { ShowOnScreenHUD = v; }

static bool sysval_getShowInterface() { return ShowInterface; }
static void sysval_setShowInterface(bool v) { ShowInterface = v; }

static bool sysval_getBattleMode() { return BattleMode; }

static bool sysval_getEndLevel() { return EndLevel; }
static void sysval_setEndLevel(bool v) { EndLevel = v; }

static int sysval_getLevelMacro() { return (int)LevelMacro; }
static void sysval_setLevelMacro(int v) { LevelMacro = (LevelMacro_t)v; }
static int sysval_getLevelMacroCounter() { return LevelMacroCounter; }

static int sysval_getLevelBeatCode() { return (int)LevelBeatCode; }
static void sysval_setLevelBeatCode(int v) { LevelBeatCode = (LevelBeatCode_t)v; }

static int sysval_getGameTime() { return (int)CommonFrame; }

static int sysval_getCheckpointCount() { return (int)CheckpointsList.size(); }
static int sysval_getCheckpointId(int index)
{
    // 1-based index matching Lua convention
    if(index < 1 || size_t(index) > CheckpointsList.size())
        return 0;
    return CheckpointsList[size_t(index) - 1].id;
}


static std::string sysval_getLevelName()
{
    return LevelName.empty() ? FileName : LevelName;
}


static int misc_getStrWidth(const std::string &text, double font)
{
    int iFont = static_cast<int>(font);
    if(iFont < 1 || iFont > 5)
        iFont = 3;
    if(text.empty())
        return 0;
    return SuperTextPixLen(text, iFont);
}

static int misc_getStrWidth(const std::string &text)
{
    return misc_getStrWidth(text, 3.0);
}

// ============================================================================
// Dynamic config access (Lua can read/write g_config options by internal name)
static luabind::object misc_getConfig(const std::string &name)
{
    lua_State* L = xtech_lua_getState();
    for(BaseConfigOption_t<true>* option : g_config.m_options)
    {
        if(!option->m_base || !option->m_base->m_internal_name)
            continue;
        if(name != option->m_base->m_internal_name)
            continue;

        // Try bool
        if(auto* opt = dynamic_cast<ConfigOption_t<true, bool>*>(option))
            return luabind::object(L, opt->m_value);
        // Try int
        if(auto* opt = dynamic_cast<ConfigOption_t<true, int>*>(option))
            return luabind::object(L, static_cast<double>(opt->m_value));
        // Try string
        if(auto* opt = dynamic_cast<ConfigOption_t<true, std::string>*>(option))
            return luabind::object(L, opt->m_value);
        // Try enum (stored as int)
        if(auto* opt = dynamic_cast<ConfigEnumOption_t<true, int>*>(option))
            return luabind::object(L, static_cast<double>(opt->m_value));

        break;
    }
    return luabind::object(); // nil
}

static void misc_setConfig(const std::string &name, double value)
{
    for(BaseConfigOption_t<true>* option : g_config.m_options)
    {
        if(!option->m_base || !option->m_base->m_internal_name)
            continue;
        if(name != option->m_base->m_internal_name)
            continue;

        // Write directly to avoid linker issues with template operator=
        // Try bool
        if(auto* opt = dynamic_cast<ConfigOption_t<true, bool>*>(option))
        {
            opt->m_value = (value != 0.0);
            opt->m_set = ConfigSetLevel::ep_compat;
            opt->_on_change();
            return;
        }
        // Try int
        if(auto* opt = dynamic_cast<ConfigOption_t<true, int>*>(option))
        {
            opt->m_value = static_cast<int>(value);
            opt->m_set = ConfigSetLevel::ep_compat;
            opt->_on_change();
            return;
        }
        // Try string
        if(auto* opt = dynamic_cast<ConfigOption_t<true, std::string>*>(option))
        {
            opt->m_value = std::to_string(static_cast<int>(value));
            opt->m_set = ConfigSetLevel::ep_compat;
            opt->_on_change();
            return;
        }
        // Try enum
        if(auto* opt = dynamic_cast<ConfigEnumOption_t<true, int>*>(option))
        {
            opt->m_value = static_cast<int>(value);
            opt->m_set = ConfigSetLevel::ep_compat;
            opt->_on_change();
            return;
        }

        break;
    }
}

// overload: set string config (supports bool, string, int, and enum types)
static void misc_setConfigStr(const std::string &name, const std::string &value)
{
    for(BaseConfigOption_t<true>* option : g_config.m_options)
    {
        if(!option->m_base || !option->m_base->m_internal_name)
            continue;
        if(name != option->m_base->m_internal_name)
            continue;

        // Try bool
        if(auto* opt = dynamic_cast<ConfigOption_t<true, bool>*>(option))
        {
            opt->m_value = (value == "1" || value == "true");
            opt->m_set = ConfigSetLevel::ep_compat;
            opt->_on_change();
            return;
        }
        // Try string
        if(auto* opt = dynamic_cast<ConfigOption_t<true, std::string>*>(option))
        {
            opt->m_value = value;
            opt->m_set = ConfigSetLevel::ep_compat;
            opt->_on_change();
            return;
        }
        // Try int
        if(auto* opt = dynamic_cast<ConfigOption_t<true, int>*>(option))
        {
            opt->m_value = std::atoi(value.c_str());
            opt->m_set = ConfigSetLevel::ep_compat;
            opt->_on_change();
            return;
        }
        // Try enum<int>
        if(auto* opt = dynamic_cast<ConfigEnumOption_t<true, int>*>(option))
        {
            opt->m_value = std::atoi(value.c_str());
            opt->m_set = ConfigSetLevel::ep_compat;
            opt->_on_change();
            return;
        }
        // Try enum<pair<int,int>> (e.g. internal-res)
        if(dynamic_cast<ConfigEnumOption_t<true, std::pair<int, int>>*>(option))
        {
            // try numeric "WxH" format first
            int w = 0, h = 0;
            if(sscanf(value.c_str(), "%dx%d", &w, &h) == 2)
            {
                if(auto* opt = dynamic_cast<ConfigEnumOption_t<true, std::pair<int, int>>*>(option))
                {
                    opt->m_value = {w, h};
                    opt->m_set = ConfigSetLevel::ep_compat;
                    opt->_on_change();
                }
            }
            else
            {
                // use config helper for string key matching (e.g. "svga", "hd1080p")
                config_set_by_name_str(name, value);
            }
            return;
        }

        break;
    }
}


// ============================================================================
// Async/Delay support (P3: named timers)

struct LuaDelayedCall
{
    int framesLeft;
    std::string name;      // P3: timer name (empty = anonymous)
    luabind::object callback;
};

static std::list<LuaDelayedCall> g_delayedCalls;

// Helper function for delayed calls (raw C bridge for luabind compatibility)
static int lua_misc_wait_raw(lua_State *L)
{
    // args: callback (at 1), frames (at 2)
    int frames = (int)luaL_checkinteger(L, 2);
    if(frames <= 0) frames = 1;
    lua_pushvalue(L, 1); // copy callback
    luabind::object cb(luabind::from_stack(L, -1));
    lua_pop(L, 1);
    g_delayedCalls.push_back({frames, std::string(), cb});
    return 0;
}

// Helper function for delayed calls, exposed to Lua
static void lua_misc_wait(luabind::object callback, int frames)
{
    if(frames <= 0) frames = 1;
    g_delayedCalls.push_back({frames, std::string(), callback});
}

// P3: Named timer - create or replace (raw C bridge)
static int lua_timer_create_raw(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    int frames = (int)luaL_checkinteger(L, 3);
    if(frames <= 0) frames = 1;
    lua_pushvalue(L, 2);
    luabind::object cb(luabind::from_stack(L, -1));
    lua_pop(L, 1);
    for(auto &dc : g_delayedCalls)
    {
        if(!dc.name.empty() && dc.name == name)
        {
            dc.framesLeft = frames;
            dc.callback = cb;
            return 0;
        }
    }
    g_delayedCalls.push_back({frames, name, cb});
    return 0;
}

// P3: Named timer - create or replace a named timer
static void lua_timer_create(const std::string &name, luabind::object callback, int frames)
{
    if(frames <= 0) frames = 1;
    for(auto &dc : g_delayedCalls)
    {
        if(!dc.name.empty() && dc.name == name)
        {
            dc.framesLeft = frames;
            dc.callback = callback;
            return;
        }
    }
    g_delayedCalls.push_back({frames, name, callback});
}

// P3: Delay-trigger a named game event (TeaScript TCreate equivalent)
// Creates a timer that fires xtech_event_triggerByName(eventName) after delay
static int lua_timer_createEvent_raw(lua_State *L)
{
    const char *eventName = luaL_checkstring(L, 1);
    int frames = (int)luaL_checkinteger(L, 2);
    if(frames <= 0) frames = 1;
    // Build a Lua callback that triggers the event
    std::string luaCode = "xtech_event_triggerByName(\"";
    luaCode += eventName;
    luaCode += "\")";
    if(luaL_loadstring(L, luaCode.c_str()) == 0)
    {
        luabind::object cb(luabind::from_stack(L, -1));
        lua_pop(L, 1);
        // Use empty name for timer (TClear by event name is done via cancel)
        // Use the event name as timer name for cancel support
        for(auto &dc : LuaMisc::g_delayedCalls)
        {
            if(!dc.name.empty() && dc.name == eventName)
            {
                dc.framesLeft = frames;
                dc.callback = cb;
                return 0;
            }
        }
        LuaMisc::g_delayedCalls.push_back({frames, eventName, cb});
    }
    else
    {
        lua_pop(L, 1); // pop error string
    }
    return 0;
}

// P3: Cancel a named timer
static void lua_timer_cancel(const std::string &name)
{
    for(auto it = g_delayedCalls.begin(); it != g_delayedCalls.end(); )
    {
        if(it->name == name)
            it = g_delayedCalls.erase(it);
        else
            ++it;
    }
}

// P3: Cancel all timers (both named and anonymous)
static void lua_timer_clearAll()
{
    g_delayedCalls.clear();
}

// ============================================================================
// Custom Character Registration APIs
// ============================================================================

// Set the resource directory name for a character slot
static void player_setName(int character, const std::string &name)
{
    if(character < 1 || character > (int)numCharacters)
        return;
    s_playerFileName[character] = name;
}

// Reload textures and INI configs for a character
static void player_reload(int character)
{
    if(character < 1 || character > (int)numCharacters)
        return;
    if(s_playerFileName[character].empty())
        return;

    int c = character - 1; // 0-based for GFXCharacterBMP
    std::string gfxDir = AppPath + "graphics/";

    // Load per-state textures — search order:
    //   1. Level custom dir     (highest priority override)
    //   2. Episode dir
    //   3. Game graphics dir    (fallback)
    DirListCI gfxSubDir;
    gfxSubDir.setCurDir(gfxDir + s_playerFileName[character]);
    for(int state = 1; state <= numStates; state++)
    {
        auto texName = fmt::sprintf_ne("%s-%d.png", s_playerFileName[character].c_str(), state);

        std::string path = g_dirCustom.resolveFileCaseExistsAbs(texName);
        if(path.empty())
            path = g_dirEpisode.resolveFileCaseExistsAbs(texName);
        if(path.empty())
            path = gfxSubDir.resolveFileCaseExistsAbs(texName);

        if(!path.empty())
        {
            GFXCharacterBMP[c][state].reset();
            XRender::LoadPicture(GFXCharacterBMP[c][state], path);
        }
    }

    // Load INI configs (episode first, then level override)
    for(int state = 1; state <= numStates; state++)
    {
        auto iniName = fmt::sprintf_ne("%s-%d.ini", s_playerFileName[character].c_str(), state);
        std::string iniPath = g_dirEpisode.resolveFileCaseExistsAbs(iniName);
        if(!iniPath.empty())
            LoadCustomPlayer(character, state, iniPath);

        iniPath = g_dirCustom.resolveFileCaseExistsAbs(iniName);
        if(!iniPath.empty())
            LoadCustomPlayer(character, state, iniPath);
    }
}

// Set physics parameters at runtime
static void player_setPhysics(int character, int state,
    const std::string &field, double value)
{
    if(character < 1 || character > (int)numCharacters)
        return;
    if(state < 1 || state > (int)numStates)
        return;

    int v = static_cast<int>(value);

    if(field == "width")
        Physics.PlayerWidth[character][state] = v;
    else if(field == "height")
        Physics.PlayerHeight[character][state] = v;
    else if(field == "duckHeight")
        Physics.PlayerDuckHeight[character][state] = v;
    else if(field == "grabX")
        Physics.PlayerGrabSpotX[character][state] = v;
    else if(field == "grabY")
        Physics.PlayerGrabSpotY[character][state] = v;
    else if(field == "accX")
        Physics.PlayerAccessoryOffsetX[character][state] = v;
    else if(field == "accY")
        Physics.PlayerAccessoryOffsetY[character][state] = v;
}

// ============================================================================
// World map API
// ============================================================================

// Helper: find WorldLevel by filename
static int worldmap_findLevelIdx(const std::string &levelName)
{
    for(int i = 1; i <= numWorldLevels; i++)
        if(WorldLevel[i].Active && WorldLevel[i].FileName == levelName)
            return i;
    return -1;
}

// Helper: lazy-init save_info for a world level (if never visited / stars not enabled)
static void worldmap_ensureSaveInfo(int idx)
{
    if(idx < 1 || idx > numWorldLevels) return;
    auto &l = WorldLevel[idx];
    if(l.save_info.inited()) return;
    if(l.FileName.empty()) return;

    std::string lFile = l.FileName;
    addMissingLvlSuffix(lFile);
    std::string fullPath = g_dirEpisode.resolveFileCaseExistsAbs(lFile);
    if(fullPath.empty()) return;

    LevelData tempData;
    l.save_info = InitLevelSaveInfo(fullPath, tempData);
}

// Returns the filename of the world level the player is standing on, or empty string
static std::string worldmap_getCurrentLevel()
{
    int idx = WorldPlayer[1].LevelIndex;
    if(idx > 0 && idx <= numWorldLevels && WorldLevel[idx].Active)
        return WorldLevel[idx].FileName;
    return std::string();
}

// World map: get medal count for a specific level by filename
static int worldmap_getLevelMedalCount(const std::string &levelName)
{
    int idx = worldmap_findLevelIdx(levelName);
    if(idx < 0) return 0;
    worldmap_ensureSaveInfo(idx);
    auto &info = WorldLevel[idx].save_info;
    return info.inited() ? info.max_medals : 0;
}

// World map: check if a medal variant is collected for a specific level
static bool worldmap_isLevelMedalGotten(const std::string &levelName, int variant, bool permanent)
{
    int idx = worldmap_findLevelIdx(levelName);
    if(idx < 0) return false;
    worldmap_ensureSaveInfo(idx);
    auto &info = WorldLevel[idx].save_info;
    if(!info.inited()) return false;
    int vi = variant - 1;
    if(vi < 0 || vi >= 8) return false;
    if(permanent)
        return (info.medals_got >> vi) & 1;
    else
        return (info.medals_best >> vi) & 1;
}

// Returns the screen X, Y of a world level by filename (second return value is Y via luabind)
static luabind::object worldmap_getLevelScreenPos(const std::string &levelName)
{
    lua_State* L = xtech_lua_getState();
    for(int i = 1; i <= numWorldLevels; i++)
    {
        if(WorldLevel[i].Active && WorldLevel[i].FileName == levelName)
        {
            // Calculate screen position from world coords
            int screenX = (int)WorldLevel[i].Location.X;
            int screenY = (int)WorldLevel[i].Location.Y;

            luabind::object t = luabind::newtable(L);
            t["x"] = screenX;
            t["y"] = screenY;
            return t;
        }
    }
    return luabind::object(); // nil
}

// smbx2compat: SMBX2 scripts assume Windows' case-insensitive paths (they require
// "classExpander" but ship "classexpander.lua"). Returns "" if nothing matches.
static std::string misc_resolveFileCI(const std::string &baseDir, const std::string &relPath)
{
    static std::map<std::string, std::unique_ptr<DirListCI>> s_dirs;
    std::unique_ptr<DirListCI> &dir = s_dirs[baseDir];
    if(!dir)
        dir.reset(new DirListCI(baseDir));
    return dir->resolveFileCaseExistsAbs(relPath);
}

// smbx2compat: {w, h} of an image file, or nil. SMBX2's Graphics.loadImage
// needs sizes when it loads; drawing the images comes later.
static luabind::object misc_imageSize(const std::string &path)
{
    lua_State* L = xtech_lua_getState();
    PGE_Size size;
    if(!GraphicsHelps::getImageMetrics(path, &size))
        return luabind::object(); // nil

    luabind::object t = luabind::newtable(L);
    t["w"] = size.w();
    t["h"] = size.h();
    return t;
}

// smbx2compat: an image's pixels for SMBX2's Graphics.getPixelData, as 32-bit
// words (0xAARRGGBB), rows top to bottom like LunaLua's. "" if it can't load.
static std::string misc_imagePixels(const std::string &path)
{
    FIBITMAP *img = GraphicsHelps::loadImage(path, true);
    if(!img)
        return std::string();

    size_t w = FreeImage_GetWidth(img);
    size_t h = FreeImage_GetHeight(img);
    size_t pitch = FreeImage_GetPitch(img);
    const uint8_t *bits = FreeImage_GetBits(img);

    std::string out(w * h * 4, '\0');
    for(size_t y = 0; y < h; y++) // FreeImage keeps rows bottom-up
        std::memcpy(&out[y * w * 4], bits + (h - 1 - y) * pitch, w * 4);

    GraphicsHelps::closeImage(img);
    return out;
}

// smbx2compat: SMBX2's Misc.listFiles / listDirectories (names only, like LunaLua)
static luabind::object misc_listDir(const std::string &path, bool wantDirs)
{
    lua_State* L = xtech_lua_getState();
    std::vector<std::string> names;
    DirMan dir(path);
    bool ok = wantDirs ? dir.getListOfFolders(names) : dir.getListOfFiles(names);
    luabind::object t = luabind::newtable(L);
    if(!ok)
        return t;
    int i = 1;
    for(const std::string &name : names)
        t[i++] = name;
    return t;
}

// smbx2compat: SMBX2's GameData (Misc.SetRawGameData) outlives each level's Lua state
static std::string s_smbx2GameData;

static void misc_setGameData(const std::string &data)
{
    s_smbx2GameData = data;
}

static std::string misc_getGameData()
{
    return s_smbx2GameData;
}

// smbx2compat: custom params (JSON) from the level file, for SMBX2's
// ffi_customparams. kind "level" returns the string; "section" a table by
// section ID; "npc", "bgo" and "block" return { ids = {...}, params = {...} }
// by engine index, where params has no entry for objects without any.
template<class T>
static luabind::object objectCustomParams(lua_State* L, T& arr, int count)
{
    luabind::object ids = luabind::newtable(L);
    luabind::object params = luabind::newtable(L);
    for(int i = 1; i <= count; i++)
    {
        ids[i] = (int)arr[i].Type;
        uint32_t p = arr[i].CustomParams;
        if(p > 0 && p < g_objectCustomParams.size())
            params[i] = g_objectCustomParams[p];
    }

    luabind::object t = luabind::newtable(L);
    t["ids"] = ids;
    t["params"] = params;
    return t;
}

static luabind::object misc_customParams(const std::string &kind)
{
    lua_State* L = xtech_lua_getState();

    if(kind == "level")
        return luabind::object(L, g_levelCustomParams);

    if(kind == "section")
    {
        luabind::object t = luabind::newtable(L);
        for(size_t i = 0; i < g_sectionCustomParams.size(); i++)
        {
            if(!g_sectionCustomParams[i].empty())
                t[i] = g_sectionCustomParams[i];
        }
        return t;
    }

    if(kind == "npc")
        return objectCustomParams(L, NPC, numNPCs);
    if(kind == "bgo")
        return objectCustomParams(L, Background, numBackground);
    if(kind == "block")
        return objectCustomParams(L, Block, numBlock);

    return luabind::object(); // nil
}

// smbx2compat: monotonic seconds, for SMBX2's Misc.clock (QueryPerformanceCounter on Windows)
static double misc_clock()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// smbx2compat: SMBX2's mem(), through TheXTech's memory emulator (mememu).
// Globals map directly. For SMBX 1.3's object arrays, SMBX2 reads each array's
// address from a pointer global (mem(0x00B25A20, FIELD_DWORD) for the players)
// and adds index * struct size + field offset; we hand out a made-up address
// per array, far from the globals, and turn those sums back into an object and
// an offset. Players, NPCs, blocks and BGOs go to mememu's per-object
// emulators; the structs it doesn't have (layers, events, warps, water) and
// all string fields go to the field tables below. Character templates
// (LunaLua's getTemplateForCharacter) are our own Player_t copies, since
// TheXTech has none.

// Names behind TheXTech's layer and event indexes ("" for none)
static std::string memLayerName(layerindex_t l)
{
    return (l == LAYER_NONE) ? std::string() : Layer[l].Name;
}

static std::string memEventName(eventindex_t e)
{
    return (e == EVENT_NONE) ? std::string() : Events[e].Name;
}

// SMBX's Location_t is six doubles: X, Y, Height, Width, SpeedX, SpeedY.
// Section bounds and event positions keep the bottom in Height and the right
// edge in Width.
static double memLocGet(const SpeedlessLocation_t &loc, int k)
{
    switch(k)
    {
    case 0:
        return (double)loc.X;
    case 1:
        return (double)loc.Y;
    case 2:
        return (double)loc.Height;
    case 3:
        return (double)loc.Width;
    default:
        return 0; // no speed
    }
}

static void memLocSet(SpeedlessLocation_t &loc, int k, double v)
{
    switch(k)
    {
    case 0:
        loc.X = num_t(v, nullptr);
        break;
    case 1:
        loc.Y = num_t(v, nullptr);
        break;
    case 2:
        loc.Height = num_t(v, nullptr);
        break;
    case 3:
        loc.Width = num_t(v, nullptr);
        break;
    default:
        break;
    }
}

static int32_t *memIntLoc(IntegerLocation_t &loc, int k)
{
    switch(k)
    {
    case 0:
        return &loc.X;
    case 1:
        return &loc.Y;
    case 2:
        return &loc.Height;
    case 3:
        return &loc.Width;
    default:
        return nullptr;
    }
}

static double memIntLocGet(IntegerLocation_t &loc, int k)
{
    int32_t *v = memIntLoc(loc, k);
    return v ? *v : 0;
}

static void memIntLocSet(IntegerLocation_t &loc, int k, double v)
{
    if(int32_t *f = memIntLoc(loc, k))
        *f = (int32_t)v;
}

// SMBX's Controls_t: ten VB6 booleans in this order
static bool &memControl(Controls_t &c, int k)
{
    switch(k)
    {
    case 0:
        return c.Up;
    case 1:
        return c.Down;
    case 2:
        return c.Left;
    case 3:
        return c.Right;
    case 4:
        return c.Jump;
    case 5:
        return c.AltJump;
    case 6:
        return c.Run;
    case 7:
        return c.AltRun;
    case 8:
        return c.Drop;
    default:
        return c.Start;
    }
}

// An event's HideLayer/ShowLayer/ToggleLayer(0 To 20); TheXTech keeps only the set ones
static std::string memLayerListGet(const std::vector<layerindex_t> &list, int k)
{
    return (k < (int)list.size()) ? memLayerName(list[k]) : std::string();
}

static void memLayerListSet(std::vector<layerindex_t> &list, int k, const std::string &name)
{
    if(k >= (int)list.size())
        list.resize(k + 1, LAYER_NONE);
    list[k] = FindLayer(name);
    list.erase(std::remove(list.begin(), list.end(), LAYER_NONE), list.end());
}

static void memSetLayerName(int i, const std::string &name)
{
    if(i < numLayers)
        RenameLayer(i, name);
    else if(i == numLayers && i <= maxLayers && !name.empty() && FindLayer(name) == LAYER_NONE)
    {
        Layer[i] = Layer_t();
        Layer[i].Name = name;
        numLayers++;
    }
}

static void memSetEventName(int i, const std::string &name)
{
    if(i < numEvents)
        RenameEvent(i, name);
    else if(i == numEvents && i <= maxEvents && !name.empty() && FindEvent(name) == EVENT_NONE)
    {
        // SMBX2's Events.add: name the first unused event
        InitializeEvent(Events[i]);
        Events[i].Name = name;
        numEvents++;
    }
}

struct MemField
{
    unsigned offset;        //!< in the SMBX 1.3 struct
    FIELDTYPE type;         //!< FT_DWORD for VB6 strings (a pointer in SMBX), which have getS/setS
    int count;              //!< elements of an array field (1 = plain field)
    unsigned stride;        //!< distance between them
    double (*get)(int i, int k);
    void (*set)(int i, int k, double v); //!< nullptr = writes dropped
    std::string (*getS)(int i, int k);
    void (*setS)(int i, int k, const std::string &v);
};

#define MF_FLAG(off, lvalue) {off, FT_WORD, 1, 0, \
    [](int i, int) -> double { return (lvalue) ? -1 : 0; }, \
    [](int i, int, double v) { (lvalue) = (v != 0); }, nullptr, nullptr}
#define MF_INT(off, ft, ctype, lvalue) {off, ft, 1, 0, \
    [](int i, int) -> double { return (double)(lvalue); }, \
    [](int i, int, double v) { (lvalue) = (ctype)v; }, nullptr, nullptr}
#define MF_FLOAT(off, lvalue) {off, FT_FLOAT, 1, 0, \
    [](int i, int) -> double { return (double)(num_t)(lvalue); }, \
    [](int i, int, double v) { (lvalue) = static_cast<numf_t>(num_t(v, nullptr)); }, nullptr, nullptr}
#define MF_NUM(off, lvalue) {off, FT_DFLOAT, 1, 0, \
    [](int i, int) -> double { return (double)(lvalue); }, \
    [](int i, int, double v) { (lvalue) = num_t(v, nullptr); }, nullptr, nullptr}
#define MF_LOC(off, loc, sync) {off, FT_DFLOAT, 6, 8, \
    [](int i, int k) -> double { return memLocGet(loc, k); }, \
    [](int i, int k, double v) { memLocSet(loc, k, v); sync; }, nullptr, nullptr}
#define MF_LAYER(off, lvalue, sync) {off, FT_DWORD, 1, 0, nullptr, nullptr, \
    [](int i, int) -> std::string { return memLayerName(lvalue); }, \
    [](int i, int, const std::string &v) { (lvalue) = FindLayer(v); sync; }}
#define MF_EVENT(off, lvalue) {off, FT_DWORD, 1, 0, nullptr, nullptr, \
    [](int i, int) -> std::string { return memEventName(lvalue); }, \
    [](int i, int, const std::string &v) { (lvalue) = FindEvent(v); }}
#define MF_TEXT(off, lvalue) {off, FT_DWORD, 1, 0, nullptr, nullptr, \
    [](int i, int) -> std::string { return GetS(lvalue); }, \
    [](int i, int, const std::string &v) { SetS(lvalue, v); }}

static constexpr int c_smbxSections = 21; // SMBX 1.3's maxSections + 1, for struct layouts

static const MemField s_npcStringFields[] =
{
    MF_LAYER(0x00, NPC[i].AttLayer, (void)0),
    MF_EVENT(0x2C, NPC[i].TriggerActivate),
    MF_EVENT(0x30, NPC[i].TriggerDeath),
    MF_EVENT(0x34, NPC[i].TriggerTalk),
    MF_EVENT(0x38, NPC[i].TriggerLast),
    MF_LAYER(0x3C, NPC[i].Layer, syncLayers_NPC(i)),
    MF_TEXT(0x4C, NPC[i].Text),
};

static const MemField s_blockStringFields[] =
{
    MF_EVENT(0x0C, Block[i].TriggerHit),
    MF_EVENT(0x10, Block[i].TriggerDeath),
    MF_EVENT(0x14, Block[i].TriggerLast),
    MF_LAYER(0x18, Block[i].Layer, syncLayersTrees_Block(i)),
};

static const MemField s_bgoStringFields[] =
{
    MF_LAYER(0x00, Background[i].Layer, syncLayers_BGO(i)),
};

static const MemField s_layerFields[] =
{
    MF_FLAG(0x00, Layer[i].EffectStop),
    {0x04, FT_DWORD, 1, 0, nullptr, nullptr,
        [](int i, int) -> std::string { return (i < numLayers) ? Layer[i].Name : std::string(); },
        [](int i, int, const std::string &v) { memSetLayerName(i, v); }},
    MF_FLAG(0x08, Layer[i].Hidden),
    // TheXTech's SetLayerSpeed also stops the layer's objects when both speeds reach 0
    {0x0C, FT_FLOAT, 1, 0,
        [](int i, int) -> double { return (double)(num_t)Layer[i].SpeedX; },
        [](int i, int, double v) { SetLayerSpeed(i, num_t(v, nullptr), (num_t)Layer[i].SpeedY); }, nullptr, nullptr},
    {0x10, FT_FLOAT, 1, 0,
        [](int i, int) -> double { return (double)(num_t)Layer[i].SpeedY; },
        [](int i, int, double v) { SetLayerSpeed(i, (num_t)Layer[i].SpeedX, num_t(v, nullptr)); }, nullptr, nullptr},
};

// SMBX2's ffi_events.lua layout (stride 0x588)
static const MemField s_eventFields[] =
{
    MF_FLAG(0x00, Events[i].LayerSmoke),
    MF_INT(0x02, FT_WORD, vbint_t, Events[i].Sound),
    {0x04, FT_DWORD, 1, 0, nullptr, nullptr,
        [](int i, int) -> std::string { return (i < numEvents) ? Events[i].Name : std::string(); },
        [](int i, int, const std::string &v) { memSetEventName(i, v); }},
    MF_TEXT(0x08, Events[i].Text),
    {0x0C, FT_DWORD, 21, 4, nullptr, nullptr,
        [](int i, int k) -> std::string { return memLayerListGet(Events[i].HideLayer, k); },
        [](int i, int k, const std::string &v) { memLayerListSet(Events[i].HideLayer, k, v); }},
    {0x60, FT_DWORD, 21, 4, nullptr, nullptr,
        [](int i, int k) -> std::string { return memLayerListGet(Events[i].ShowLayer, k); },
        [](int i, int k, const std::string &v) { memLayerListSet(Events[i].ShowLayer, k, v); }},
    {0xB4, FT_DWORD, 21, 4, nullptr, nullptr,
        [](int i, int k) -> std::string { return memLayerListGet(Events[i].ToggleLayer, k); },
        [](int i, int k, const std::string &v) { memLayerListSet(Events[i].ToggleLayer, k, v); }},
    {0x108, FT_WORD, c_smbxSections, 2,
        [](int i, int k) -> double { return Events[i].section[k].music_id; },
        [](int i, int k, double v) { Events[i].section[k].music_id = (vbint_t)v; }, nullptr, nullptr},
    {0x132, FT_WORD, c_smbxSections, 2,
        [](int i, int k) -> double { return Events[i].section[k].background_id; },
        [](int i, int k, double v) { Events[i].section[k].background_id = (vbint_t)v; }, nullptr, nullptr},
    {0x15C, FT_DFLOAT, c_smbxSections * 6, 8,
        [](int i, int k) -> double { return memIntLocGet(Events[i].section[k / 6].position, k % 6); },
        [](int i, int k, double v) { memIntLocSet(Events[i].section[k / 6].position, k % 6, v); }, nullptr, nullptr},
    MF_INT(0x54C, FT_WORD, int, Events[i].EndGame),
    MF_EVENT(0x550, Events[i].TriggerEvent),
    MF_INT(0x554, FT_DFLOAT, vbint_t, Events[i].TriggerDelay),
    {0x55C, FT_WORD, 10, 2,
        [](int i, int k) -> double { return memControl(Events[i].Controls, k) ? -1 : 0; },
        [](int i, int k, double v) { memControl(Events[i].Controls, k) = (v != 0); }, nullptr, nullptr},
    MF_LAYER(0x570, Events[i].MoveLayer, (void)0),
    MF_FLOAT(0x574, Events[i].SpeedX),
    MF_FLOAT(0x578, Events[i].SpeedY),
    MF_FLOAT(0x57C, Events[i].AutoX),
    MF_FLOAT(0x580, Events[i].AutoY),
    MF_INT(0x584, FT_WORD, int, Events[i].AutoSection),
    MF_FLAG(0x586, Events[i].AutoStart),
};

static const MemField s_warpFields[] =
{
    MF_FLAG(0x00, Warp[i].Locked),
    MF_FLAG(0x02, Warp[i].WarpNPC),
    MF_FLAG(0x04, Warp[i].NoYoshi),
    MF_LAYER(0x08, Warp[i].Layer, syncLayers_Warp(i)),
    MF_FLAG(0x0C, Warp[i].Hidden),
    MF_FLAG(0x0E, Warp[i].PlacedEnt),
    MF_FLAG(0x10, Warp[i].PlacedExit),
    MF_INT(0x12, FT_WORD, vbint_t, Warp[i].Stars),
    MF_LOC(0x14, Warp[i].Entrance, (void)0),
    MF_LOC(0x44, Warp[i].Exit, (void)0),
    MF_INT(0x74, FT_WORD, vbint_t, Warp[i].Effect),
    MF_TEXT(0x78, Warp[i].level),
    MF_INT(0x7C, FT_WORD, vbint_t, Warp[i].LevelWarp),
    MF_FLAG(0x7E, Warp[i].LevelEnt),
    MF_INT(0x80, FT_WORD, int8_t, Warp[i].Direction),
    MF_INT(0x82, FT_WORD, int8_t, Warp[i].Direction2),
    MF_FLAG(0x84, Warp[i].MapWarp),
    MF_INT(0x86, FT_WORD, vbint_t, Warp[i].MapX),
    MF_INT(0x88, FT_WORD, vbint_t, Warp[i].MapY),
    MF_INT(0x8A, FT_WORD, uint8_t, Warp[i].curStars),
};

static const MemField s_waterFields[] =
{
    MF_LAYER(0x00, Water[i].Layer, syncLayers_Water(i)),
    MF_FLAG(0x04, Water[i].Hidden),
    {0x08, FT_FLOAT, 1, 0, [](int, int) -> double { return 0; }, nullptr, nullptr, nullptr}, // Buoy: unused by SMBX
    {0x0C, FT_WORD, 1, 0,
        [](int i, int) -> double { return (Water[i].Type == PHYSID_QUICKSAND) ? -1 : 0; },
        [](int i, int, double v) { Water[i].Type = (v != 0) ? PHYSID_QUICKSAND : PHYSID_WATER; }, nullptr, nullptr},
    MF_LOC(0x10, Water[i].Location, syncLayers_Water(i)),
};

// vScreen(0 To 2): each camera's place on the screen and size
static const MemField s_cameraFields[] =
{
    MF_INT(0x00, FT_DFLOAT, int, vScreen[i].Left),
    MF_INT(0x08, FT_DFLOAT, int, vScreen[i].Top),
    MF_INT(0x10, FT_DFLOAT, int, vScreen[i].Width),
    MF_INT(0x18, FT_DFLOAT, int, vScreen[i].Height),
    MF_FLAG(0x20, vScreen[i].Visible),
    MF_NUM(0x24, vScreen[i].tempX),
    MF_NUM(0x2C, vScreen[i].TempY),
    MF_INT(0x34, FT_WORD, int, vScreen[i].TempDelay),
};

// NewEvent(1 To 100): names of the queued events
static const MemField s_newEventFields[] =
{
    MF_EVENT(0x00, NewEvent[i]),
};

// CustomMusic(0 To maxSections): each section's music file
static const MemField s_sectionMusicFields[] =
{
    {0x00, FT_DWORD, 1, 0, nullptr, nullptr,
        [](int i, int) -> std::string { return CustomMusic[i]; },
        [](int i, int, const std::string &v) { CustomMusic[i] = v; }},
};

#undef MF_FLAG
#undef MF_INT
#undef MF_FLOAT
#undef MF_NUM
#undef MF_LOC
#undef MF_LAYER
#undef MF_EVENT
#undef MF_TEXT

enum ObjMemKind
{
    OBJMEM_PLAYER,
    OBJMEM_NPC,
    OBJMEM_BLOCK,
    OBJMEM_BGO,
    OBJMEM_TEMPLATE,
    OBJMEM_FIELDS, //!< only the field table
};

struct ObjMemArray
{
    unsigned pointerGlobal; //!< where SMBX 1.3 keeps the array's address (0 = none)
    ObjMemKind kind;
    unsigned base;          //!< made-up address of the first element
    unsigned size;          //!< struct size in SMBX 1.3
    int first;              //!< index of the first element (VB6 lower bound)
    int last;
    const MemField *fields; //!< looked at before mememu
    size_t numFields;
};

static constexpr int c_numTemplates = 32;
static Player_t s_templatePlayers[c_numTemplates];

#define MEM_FIELDS(table) table, sizeof(table) / sizeof(table[0])

static const ObjMemArray s_objMemArrays[] =
{
    {0x00B25A20, OBJMEM_PLAYER,   0x10000000, 0x184, 0,    maxPlayers, nullptr, 0},
    {0x00B259E8, OBJMEM_NPC,      0x11000000, 0x158, -128, maxNPCs, MEM_FIELDS(s_npcStringFields)},
    {0x00B25A04, OBJMEM_BLOCK,    0x12000000, 0x68,  0,    maxBlocks, MEM_FIELDS(s_blockStringFields)},
    {0x00B259B0, OBJMEM_BGO,      0x13000000, 0x38,  1,    maxBackgrounds + maxWarps, MEM_FIELDS(s_bgoStringFields)},
    {0,          OBJMEM_TEMPLATE, 0x14000000, 0x184, 0,    c_numTemplates - 1, nullptr, 0},
    {0x00B2C6B0, OBJMEM_FIELDS,   0x15000000, 0x14,  0,    maxLayers, MEM_FIELDS(s_layerFields)},
    {0x00B2C6CC, OBJMEM_FIELDS,   0x16000000, 0x588, 0,    maxEvents, MEM_FIELDS(s_eventFields)},
    {0x00B258F4, OBJMEM_FIELDS,   0x17000000, 0x90,  1,    maxWarps, MEM_FIELDS(s_warpFields)},
    {0x00B256F4, OBJMEM_FIELDS,   0x18000000, 0x40,  0,    maxWater, MEM_FIELDS(s_waterFields)},
    {0x00B2D6E8, OBJMEM_FIELDS,   0x19000000, 0x4,   1,    maxEvents, MEM_FIELDS(s_newEventFields)},
    {0x00B257B8, OBJMEM_FIELDS,   0x19100000, 0x4,   0,    maxSections, MEM_FIELDS(s_sectionMusicFields)},
    {0x00B25124, OBJMEM_FIELDS,   0x19200000, 0x38,  0,    c_vScreenCount, MEM_FIELDS(s_cameraFields)},
};

#undef MEM_FIELDS

static const ObjMemArray *objMemArrayAt(unsigned addr, int &index, unsigned &offset)
{
    for(const auto &a : s_objMemArrays)
    {
        unsigned end = a.base + a.size * (unsigned)(a.last - a.first + 1);
        if(addr >= a.base && addr < end)
        {
            index = a.first + (int)((addr - a.base) / a.size);
            offset = (addr - a.base) % a.size;
            return &a;
        }
    }
    return nullptr;
}

// The field of a at offset, and the element k of an array field
static const MemField *memFieldAt(const ObjMemArray &a, unsigned offset, int &k)
{
    for(size_t f = 0; f < a.numFields; f++)
    {
        const MemField &m = a.fields[f];
        if(offset < m.offset)
            continue;

        unsigned rel = offset - m.offset;
        if(m.count == 1 ? rel == 0 : (rel % m.stride == 0 && rel / m.stride < (unsigned)m.count))
        {
            k = (m.count == 1) ? 0 : (int)(rel / m.stride);
            return &m;
        }
    }
    return nullptr;
}

// smbx2compat: SMBX 1.3's per-type arrays (NPCWidth(0 To maxNPCType), BlockFrame,
// vScreenX, ...) and LunaLua's extended NPC/block properties, which SMBX2 finds
// with Misc.__getNPCPropertyTableAddress. npcconfig_core.lua reads them at
// address + stride * ID. Each table gets a made-up range; IDs the engine has map
// onto its traits (all of SMBX2's since the engine took SMBX2's limits), and
// the extended properties, which TheXTech doesn't use, are kept as plain
// storage, so values set from npc-N.txt or Lua read back.
// SMBX's list of sizable blocks, top to bottom; TheXTech draws them without one
static std::vector<int> s_sizableBlocks;

static int memSizableBlockCount()
{
    s_sizableBlocks.clear();
    for(int i = 1; i <= numBlock; i++)
    {
        int type = Block[i].Type;
        if(type >= 0 && type <= maxBlockType && BlockIsSizable[type] && !Block[i].Hidden)
            s_sizableBlocks.push_back(i);
    }
    std::stable_sort(s_sizableBlocks.begin(), s_sizableBlocks.end(),
                     [](int a, int b) { return Block[a].Location.Y < Block[b].Location.Y; });
    return (int)s_sizableBlocks.size();
}

static double memSizableBlock(int i)
{
    return (i >= 1 && i <= (int)s_sizableBlocks.size()) ? s_sizableBlocks[i - 1] : 0;
}

struct MemTypeArray
{
    unsigned pointerGlobal; //!< where SMBX 1.3 keeps the array's address (0 = none)
    const char *kind;       //!< "npc"/"block" for LunaLua's extended properties, else nullptr
    const char *name;       //!< extended property name
    FIELDTYPE type;
    int first;              //!< index of the first element (VB6 lower bound)
    int last;               //!< last index the engine has (get/set); past it, storage
    int storeLast;          //!< last index scripts may use
    double (*get)(int i);   //!< nullptr = storage only
    void (*set)(int i, double v); //!< nullptr = writes to engine-backed indices are dropped
};

static constexpr int c_smbx2NpcMaxId = 1000;   // LunaLua's NPC::MAX_ID
static constexpr int c_smbx2BlockMaxId = 2000; // Block::MAX_ID
static constexpr int c_smbx2BgoMaxId = 1000;   // SMBX_BGO::MAX_ID

#define MEM_NPC_TRAIT(field) \
    [](int i) -> double { return (double)NPCTraits[i].field; }, \
    [](int i, double v) { NPCTraits[i].field = (int16_t)v; }
#define MEM_NPC_FLAG(field) \
    [](int i) -> double { return NPCTraits[i].field ? -1 : 0; }, \
    [](int i, double v) { NPCTraits[i].field = (v != 0); }
#define MEM_NPC_CONST(func) \
    [](int i) -> double { return func(NPCID(i)) ? -1 : 0; }, nullptr
#define MEM_ARRAY(arr, ctype) \
    [](int i) -> double { return (double)arr[i]; }, \
    [](int i, double v) { arr[i] = (ctype)v; }
#define MEM_FLAGS(arr) \
    [](int i) -> double { return arr[i] ? -1 : 0; }, \
    [](int i, double v) { arr[i] = (v != 0); }

#define MEM_NPC_TABLE(ptr, type) ptr, nullptr, nullptr, type, 0, maxNPCType, c_smbx2NpcMaxId
#define MEM_BLOCK_TABLE(ptr, type, first) ptr, nullptr, nullptr, type, first, maxBlockType, c_smbx2BlockMaxId

static const MemTypeArray s_memTypeArrays[] =
{
    {MEM_NPC_TABLE(0x00B25B70, FT_WORD), MEM_NPC_TRAIT(FrameOffsetX)},
    {MEM_NPC_TABLE(0x00B25B8C, FT_WORD), MEM_NPC_TRAIT(FrameOffsetY)},
    {MEM_NPC_TABLE(0x00B25BA8, FT_WORD), MEM_NPC_TRAIT(TWidth)},
    {MEM_NPC_TABLE(0x00B25BC4, FT_WORD), MEM_NPC_TRAIT(THeight)},
    {MEM_NPC_TABLE(0x00B25BE0, FT_WORD), MEM_NPC_TRAIT(WidthGFX)},
    {MEM_NPC_TABLE(0x00B25BFC, FT_WORD), MEM_NPC_TRAIT(HeightGFX)},
    {MEM_NPC_TABLE(0x00B25C18, FT_FLOAT),
        [](int i) -> double { return (double)(num_t)NPCTraits[i].Speedvar; },
        [](int i, double v) { NPCTraits[i].Speedvar = static_cast<numf_t>(num_t(v, nullptr)); }},
    {MEM_NPC_TABLE(0x00B25C34, FT_WORD), MEM_NPC_FLAG(IsAShell)},
    {MEM_NPC_TABLE(0x00B25C50, FT_WORD), MEM_NPC_FLAG(IsABlock)},
    {MEM_NPC_TABLE(0x00B25C6C, FT_WORD), MEM_NPC_FLAG(IsAHit1Block)},
    {MEM_NPC_TABLE(0x00B25C88, FT_WORD), MEM_NPC_FLAG(IsABonus)},
    {MEM_NPC_TABLE(0x00B25CA4, FT_WORD), MEM_NPC_FLAG(IsACoin)},
    {MEM_NPC_TABLE(0x00B25CC0, FT_WORD), MEM_NPC_FLAG(IsAVine)},
    {MEM_NPC_TABLE(0x00B25CDC, FT_WORD), MEM_NPC_CONST(NPCIsAnExit)},
    {MEM_NPC_TABLE(0x00B25CF8, FT_WORD), MEM_NPC_CONST(NPCIsAParaTroopa)},
    {MEM_NPC_TABLE(0x00B25D14, FT_WORD), MEM_NPC_FLAG(IsFish)},
    {MEM_NPC_TABLE(0x00B25D30, FT_WORD), MEM_NPC_FLAG(JumpHurt)},
    {MEM_NPC_TABLE(0x00B25D4C, FT_WORD), MEM_NPC_FLAG(NoClipping)},
    {MEM_NPC_TABLE(0x00B25D68, FT_WORD),
        [](int i) -> double { return (double)NPCTraits[i].Score; },
        [](int i, double v) { NPCTraits[i].Score = (int8_t)v; }},
    {MEM_NPC_TABLE(0x00B25D84, FT_WORD), MEM_NPC_FLAG(CanWalkOn)},
    {MEM_NPC_TABLE(0x00B25DA0, FT_WORD), MEM_NPC_FLAG(GrabFromTop)},
    {MEM_NPC_TABLE(0x00B25DBC, FT_WORD), MEM_NPC_FLAG(TurnsAtCliffs)},
    {MEM_NPC_TABLE(0x00B25DD8, FT_WORD), MEM_NPC_FLAG(WontHurt)},
    {MEM_NPC_TABLE(0x00B25DF4, FT_WORD), MEM_NPC_FLAG(MovesPlayer)},
    {MEM_NPC_TABLE(0x00B25E10, FT_WORD), MEM_NPC_FLAG(StandsOnPlayer)},
    {MEM_NPC_TABLE(0x00B25E2C, FT_WORD), MEM_NPC_FLAG(IsGrabbable)},
    {MEM_NPC_TABLE(0x00B25E48, FT_WORD), MEM_NPC_CONST(NPCIsBoot)},
    {MEM_NPC_TABLE(0x00B25E64, FT_WORD), MEM_NPC_CONST(NPCIsYoshi)},
    {MEM_NPC_TABLE(0x00B25E80, FT_WORD), MEM_NPC_CONST(NPCIsToad)},
    {MEM_NPC_TABLE(0x00B25E9C, FT_WORD), MEM_NPC_FLAG(NoYoshi)},
    {MEM_NPC_TABLE(0x00B25EB8, FT_WORD), MEM_NPC_FLAG(Foreground)},
    {MEM_NPC_TABLE(0x00B25ED4, FT_WORD), MEM_NPC_CONST(NPCIsABot)},
    {MEM_NPC_TABLE(0x00B25EF0, FT_WORD), MEM_NPC_CONST(NPCDefaultMovement)},
    {MEM_NPC_TABLE(0x00B25F0C, FT_WORD), MEM_NPC_CONST(NPCIsVeggie)},
    {MEM_NPC_TABLE(0x00B25F28, FT_WORD), MEM_NPC_FLAG(NoFireBall)},
    {MEM_NPC_TABLE(0x00B25F44, FT_WORD), MEM_NPC_FLAG(NoIceBall)},
    {MEM_NPC_TABLE(0x00B25F60, FT_WORD), MEM_NPC_FLAG(NoGravity)},
    {MEM_NPC_TABLE(0x00B25F7C, FT_WORD), MEM_NPC_TRAIT(TFrames)},
    {MEM_NPC_TABLE(0x00B25F98, FT_WORD), MEM_NPC_TRAIT(FrameSpeed)},
    {MEM_NPC_TABLE(0x00B25FB4, FT_WORD), MEM_NPC_TRAIT(FrameStyle)},

    {MEM_BLOCK_TABLE(0x00B2B930, FT_WORD, 0), MEM_FLAGS(BlockIsSizable)},
    {MEM_BLOCK_TABLE(0x00B2B94C, FT_WORD, 0), MEM_ARRAY(BlockSlope, vbint_t)},
    {MEM_BLOCK_TABLE(0x00B2B968, FT_WORD, 0), MEM_ARRAY(BlockSlope2, vbint_t)},
    {MEM_BLOCK_TABLE(0x00B2B9F8, FT_WORD, 0), MEM_ARRAY(BlockWidth, vbint_t)},
    {MEM_BLOCK_TABLE(0x00B2BA14, FT_WORD, 0), MEM_ARRAY(BlockHeight, vbint_t)},
    {MEM_BLOCK_TABLE(0x00B2BEA0, FT_WORD, 1), MEM_ARRAY(BlockFrame, vbint_t)},
    {MEM_BLOCK_TABLE(0x00B2BEBC, FT_WORD, 1), MEM_ARRAY(BlockFrame2, vbint_t)},
    {MEM_BLOCK_TABLE(0x00B2C048, FT_WORD, 0), MEM_FLAGS(BlockOnlyHitspot1)},
    {MEM_BLOCK_TABLE(0x00B2C064, FT_WORD, 0), MEM_FLAGS(BlockKills)},
    {MEM_BLOCK_TABLE(0x00B2C080, FT_WORD, 0), MEM_FLAGS(BlockKills2)},
    {MEM_BLOCK_TABLE(0x00B2C09C, FT_WORD, 0), MEM_FLAGS(BlockHurts)},
    {MEM_BLOCK_TABLE(0x00B2C0B8, FT_WORD, 0), MEM_FLAGS(BlockPSwitch)},
    {MEM_BLOCK_TABLE(0x00B2C0D4, FT_WORD, 0), MEM_FLAGS(BlockNoClipping)},

    // Blocks being bumped (iBlock(0 To maxBlocks); the count is a mememu global)
    {0x00B25798, nullptr, nullptr, FT_WORD, 0, maxBlocks, maxBlocks, MEM_ARRAY(iBlock, vbint_t)},

    // SMBX's block lookup by 32-pixel column (FirstBlock/LastBlock(-8000 To 8000)) into
    // its X-sorted blocks. TheXTech uses a quadtree instead, so every column spans all
    // blocks; SMBX2's Block.iterateIntersecting still checks each one.
    {0x00B25758, nullptr, nullptr, FT_WORD, -8000, 8000, 8000,
        [](int) -> double { return 1; }, nullptr},
    {0x00B25774, nullptr, nullptr, FT_WORD, -8000, 8000, 8000,
        [](int) -> double { return numBlock; }, nullptr},

    // Sizable blocks (sBlockArray(1 To 1000)), rebuilt when SMBX2 reads the count
    {0x00B2BED8, nullptr, nullptr, FT_WORD, 1, 1000, 1000,
        [](int i) -> double { return memSizableBlock(i); }, nullptr},

    // Climbable BGOs (bgoconfig.lua's "climbable")
    {0x00B25B54, nullptr, nullptr, FT_WORD, 0, maxBackgroundType, c_smbx2BgoMaxId, MEM_FLAGS(BackgroundFence)},

    // Camera position, negated (vScreenX(0 To maxPlayers))
    {0x00B2B984, nullptr, nullptr, FT_DFLOAT, 0, c_vScreenCount, c_vScreenCount,
        [](int i) -> double { return (double)vScreen[i].X; },
        [](int i, double v) { vScreen[i].X = num_t(v, nullptr); }},
    {0x00B2B9A0, nullptr, nullptr, FT_DFLOAT, 0, c_vScreenCount, c_vScreenCount,
        [](int i) -> double { return (double)vScreen[i].Y; },
        [](int i, double v) { vScreen[i].Y = num_t(v, nullptr); }},

    // Sections (0 To maxSections). Bounds are six doubles per section (see memLocGet), flattened
    {0x00B257D4, nullptr, nullptr, FT_DFLOAT, 0, (maxSections + 1) * 6 - 1, (maxSections + 1) * 6 - 1,
        [](int i) -> double { return memLocGet(level[i / 6], i % 6); },
        [](int i, double v) { memLocSet(level[i / 6], i % 6, v); }},
    {0x00B2587C, nullptr, nullptr, FT_DFLOAT, 0, (maxSections + 1) * 6 - 1, (maxSections + 1) * 6 - 1,
        [](int i) -> double { return memIntLocGet(LevelREAL[i / 6], i % 6); },
        [](int i, double v) { memIntLocSet(LevelREAL[i / 6], i % 6, v); }},
    {0x00B257F0, nullptr, nullptr, FT_WORD, 0, maxSections, maxSections, MEM_FLAGS(LevelWrap)},
    {0x00B2580C, nullptr, nullptr, FT_WORD, 0, maxSections, maxSections, MEM_FLAGS(OffScreenExit)},
    {0x00B25828, nullptr, nullptr, FT_WORD, 0, maxSections, maxSections, MEM_ARRAY(bgMusic, vbint_t)},
    {0x00B25844, nullptr, nullptr, FT_WORD, 0, maxSections, maxSections, MEM_ARRAY(bgMusicREAL, vbint_t)},
    {0x00B25860, nullptr, nullptr, FT_WORD, 0, maxSections, maxSections, MEM_ARRAY(Background2REAL, vbint_t)},
    {0x00B258B8, nullptr, nullptr, FT_WORD, 0, maxSections, maxSections, MEM_ARRAY(Background2, vbint_t)},
    {0x00B2C5EC, nullptr, nullptr, FT_WORD, 0, maxSections, maxSections, MEM_FLAGS(NoTurnBack)},
    {0x00B2C608, nullptr, nullptr, FT_WORD, 0, maxSections, maxSections, MEM_FLAGS(UnderWater)},

    // Delays of the queued events (the names are NewEvent, in s_objMemArrays)
    {0x00B2D704, nullptr, nullptr, FT_WORD, 1, maxEvents, maxEvents, MEM_ARRAY(newEventDelay, vbint_t)},

    // World map and effect animation frames
    {0x00B2BEF8, nullptr, nullptr, FT_WORD, 1, maxSceneType, maxSceneType, MEM_ARRAY(SceneFrame, vbint_t)},
    {0x00B2BF30, nullptr, nullptr, FT_WORD, 0, 9, 100, MEM_ARRAY(SpecialFrame, vbint_t)},
    {0x00B2BFA0, nullptr, nullptr, FT_WORD, 1, maxTileType, maxTileType, MEM_ARRAY(TileFrame, vbint_t)},
    {0x00B2BFD8, nullptr, nullptr, FT_WORD, 1, 100, 100, MEM_ARRAY(LevelFrame, vbint_t)},

    // LunaLua's extended properties (NPCs.cpp/Blocks.cpp GetPropertyTableAddress)
    {0, "npc", "vulnerableharmtypes",  FT_DWORD,  0, -1, c_smbx2NpcMaxId, nullptr, nullptr},
    {0, "npc", "spinjumpsafe",         FT_WORD,   0, -1, c_smbx2NpcMaxId, nullptr, nullptr},
    {0, "npc", "nowaterphysics",       FT_WORD,   0, -1, c_smbx2NpcMaxId, nullptr, nullptr},
    {0, "npc", "harmlessgrab",         FT_WORD,   0, -1, c_smbx2NpcMaxId, nullptr, nullptr},
    {0, "npc", "harmlessthrown",       FT_WORD,   0, -1, c_smbx2NpcMaxId, nullptr, nullptr},
    {0, "npc", "ignorethrownnpcs",     FT_WORD,   0, -1, c_smbx2NpcMaxId, nullptr, nullptr},
    {0, "npc", "linkshieldable",       FT_WORD,   0, -1, c_smbx2NpcMaxId, nullptr, nullptr},
    {0, "npc", "noshieldfireeffect",   FT_WORD,   0, -1, c_smbx2NpcMaxId, nullptr, nullptr},
    {0, "npc", "notcointransformable", FT_WORD,   0, -1, c_smbx2NpcMaxId, nullptr, nullptr},
    {0, "npc", "staticdirection",      FT_WORD,   0, -1, c_smbx2NpcMaxId, nullptr, nullptr},
    {0, "npc", "luahandlesspeed",      FT_WORD,   0, -1, c_smbx2NpcMaxId, nullptr, nullptr},
    {0, "npc", "terminalvelocity",     FT_DFLOAT, 0, -1, c_smbx2NpcMaxId, nullptr, nullptr},
    {0, "npc", "falloffvineonstomp",   FT_WORD,   0, -1, c_smbx2NpcMaxId, nullptr, nullptr},
    {0, "block", "bumpable",           FT_WORD,   0, -1, c_smbx2BlockMaxId, nullptr, nullptr},
    {0, "block", "playerfilter",       FT_WORD,   0, -1, c_smbx2BlockMaxId, nullptr, nullptr},
    {0, "block", "npcfilter",          FT_WORD,   0, -1, c_smbx2BlockMaxId, nullptr, nullptr},
    {0, "block", "walkpaststair",      FT_WORD,   0, -1, c_smbx2BlockMaxId, nullptr, nullptr},
};

#undef MEM_NPC_TRAIT
#undef MEM_NPC_FLAG
#undef MEM_NPC_CONST
#undef MEM_ARRAY
#undef MEM_FLAGS
#undef MEM_NPC_TABLE
#undef MEM_BLOCK_TABLE

static constexpr size_t c_numMemTypeArrays = sizeof(s_memTypeArrays) / sizeof(s_memTypeArrays[0]);
static constexpr unsigned c_memTypeArraysBase = 0x20000000;
static constexpr unsigned c_memTypeArraySpan = 0x10000; // per table; 2001 doubles fit

static unsigned memFieldSize(FIELDTYPE ft)
{
    switch(ft)
    {
    case FT_BYTE:
        return 1;
    case FT_WORD:
        return 2;
    case FT_DFLOAT:
        return 8;
    default:
        return 4;
    }
}

// Storage behind each table, made on first use
static std::vector<uint8_t> &memTypeArrayStore(size_t t)
{
    static std::vector<uint8_t> s_stores[c_numMemTypeArrays];
    std::vector<uint8_t> &s = s_stores[t];
    if(s.empty())
    {
        const MemTypeArray &a = s_memTypeArrays[t];
        s.resize(memFieldSize(a.type) * (size_t)(a.storeLast - a.first + 1) + 8);
        // LunaLua's NPC::InitProperties: everything 0 except terminal velocity
        if(a.name && std::strcmp(a.name, "terminalvelocity") == 0)
        {
            for(int i = 0; i <= a.storeLast - a.first; i++)
            {
                double v = 8.0;
                std::memcpy(s.data() + i * 8, &v, 8);
            }
        }
    }
    return s;
}

static const MemTypeArray *memTypeArrayAt(unsigned addr, size_t &t, int &index, unsigned &offset)
{
    if(addr < c_memTypeArraysBase || addr >= c_memTypeArraysBase + c_memTypeArraySpan * c_numMemTypeArrays)
        return nullptr;

    t = (addr - c_memTypeArraysBase) / c_memTypeArraySpan;
    const MemTypeArray &a = s_memTypeArrays[t];
    unsigned size = memFieldSize(a.type);
    unsigned rel = (addr - c_memTypeArraysBase) % c_memTypeArraySpan;
    index = a.first + (int)(rel / size);
    offset = rel % size;
    if(index > a.storeLast)
        return nullptr;
    return &a;
}

static double memTypeArrayGet(size_t t, int index, unsigned offset, FIELDTYPE ft)
{
    const MemTypeArray &a = s_memTypeArrays[t];
    if(a.get && offset == 0 && index <= a.last)
        return a.get(index);

    std::vector<uint8_t> &s = memTypeArrayStore(t);
    size_t pos = memFieldSize(a.type) * (size_t)(index - a.first) + offset;
    if(pos + memFieldSize(ft) > s.size())
        return 0;

    const uint8_t *p = s.data() + pos;
    switch(ft)
    {
    case FT_BYTE:
        return *p;
    case FT_WORD:
    {
        int16_t v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }
    case FT_FLOAT:
    {
        float v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }
    case FT_DFLOAT:
    {
        double v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }
    default:
    {
        int32_t v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }
    }
}

static void memTypeArraySet(size_t t, int index, unsigned offset, FIELDTYPE ft, double value)
{
    const MemTypeArray &a = s_memTypeArrays[t];
    if(a.get && offset == 0 && index <= a.last)
    {
        if(a.set)
            a.set(index, value);
        else
        {
            static std::set<size_t> s_logged;
            if(s_logged.insert(t).second)
                pLogWarning("smbx2compat: mem() write to read-only type array 0x%08X dropped", a.pointerGlobal);
        }
        return;
    }

    std::vector<uint8_t> &s = memTypeArrayStore(t);
    size_t pos = memFieldSize(a.type) * (size_t)(index - a.first) + offset;
    if(pos + memFieldSize(ft) > s.size())
        return;

    uint8_t *p = s.data() + pos;
    switch(ft)
    {
    case FT_BYTE:
        *p = (uint8_t)(int)value;
        break;
    case FT_WORD:
    {
        int16_t v = (int16_t)(int32_t)value;
        std::memcpy(p, &v, sizeof(v));
        break;
    }
    case FT_FLOAT:
    {
        float v = (float)value;
        std::memcpy(p, &v, sizeof(v));
        break;
    }
    case FT_DFLOAT:
        std::memcpy(p, &value, sizeof(value));
        break;
    default:
    {
        int32_t v = (int32_t)(int64_t)value;
        std::memcpy(p, &v, sizeof(v));
        break;
    }
    }
}

// Address of LunaLua's extended property table (kind "npc" or "block"), 0 if unknown
static unsigned mem_propertyTableAddress(const std::string &kind, const std::string &name)
{
    for(size_t t = 0; t < c_numMemTypeArrays; t++)
    {
        const MemTypeArray &a = s_memTypeArrays[t];
        if(a.kind && kind == a.kind && name == a.name)
            return c_memTypeArraysBase + c_memTypeArraySpan * (unsigned)t;
    }
    return 0;
}

// Each unknown address is logged once
static void mem_unknown(const char *what, unsigned addr)
{
    static std::set<unsigned> s_logged;
    if(s_logged.size() < 100 && s_logged.insert(addr).second)
        pLogWarning("smbx2compat: mem() %s outside emulated memory: 0x%08X", what, addr);
}

// Every made-up array range starts here; SMBX's own globals (the pointer
// globals among them) are far below it
static constexpr unsigned c_memArraysLowest = 0x10000000;

static double mem_get(unsigned int addr, int ftype)
{
    FIELDTYPE ft = static_cast<FIELDTYPE>(ftype);

    // Almost every read is an object field; skip the ~130 pointer globals for those
    if(addr < c_memArraysLowest)
    {
        for(const auto &a : s_objMemArrays)
        {
            if(a.pointerGlobal != 0 && addr == a.pointerGlobal)
                return (double)a.base;
        }

        for(size_t t = 0; t < c_numMemTypeArrays; t++)
        {
            if(s_memTypeArrays[t].pointerGlobal != 0 && addr == s_memTypeArrays[t].pointerGlobal)
                return (double)(c_memTypeArraysBase + c_memTypeArraySpan * (unsigned)t);
        }

        // sBlockNum: the sizable list (sBlockArray) is rebuilt when SMBX2 reads its length
        if(addr == 0x00B2BEE4)
            return memSizableBlockCount();
    }

    int index;
    unsigned offset;
    size_t table;
    if(memTypeArrayAt(addr, table, index, offset))
        return memTypeArrayGet(table, index, offset, ft);

    if(const ObjMemArray *a = objMemArrayAt(addr, index, offset))
    {
        int k;
        if(const MemField *f = memFieldAt(*a, offset, k))
        {
            // A string field read as a DWORD is SMBX's string pointer: 0 when empty
            if(f->getS)
                return f->getS(index, k).empty() ? 0 : 1;
            return f->get ? f->get(index, k) : 0;
        }

        switch(a->kind)
        {
        case OBJMEM_FIELDS:
            break;
        case OBJMEM_PLAYER:
            return (double)GetMem(&Player[index], offset, ft);
        case OBJMEM_NPC:
            return (double)GetMem(&NPC[index], offset, ft);
        case OBJMEM_BLOCK:
            return (double)GetMem(&Block[index], offset, ft);
        case OBJMEM_BGO:
            return (double)GetMem(&Background[index], offset, ft);
        case OBJMEM_TEMPLATE:
            return (double)GetMem(&s_templatePlayers[index], offset, ft);
        }

        mem_unknown("read", addr);
        return 0;
    }

    if(addr >= GM_BASE && addr < GM_END)
        return (double)GetMem(addr, ft);

    mem_unknown("read", addr);
    return 0;
}

// The engine indexes its per-type arrays (BlockWidth, NPCTraits, ...) with an
// object's type, so a script giving one an SMBX2-only ID past the engine's
// limits gets ID 1 instead, as level loading does
static num_t memEngineType(num_t value, int maxType, const char *what)
{
    int type = (int)value;
    if(type >= 0 && type <= maxType)
        return value;

    static std::set<std::string> s_logged;
    std::string key = fmt::format_ne("{0}-{1}", what, type);
    if(s_logged.insert(key).second)
        pLogWarning("smbx2compat: %s is past the engine's types (max %d), using %s-1", key.c_str(), maxType, what);
    return num_t(1);
}

static void mem_set(unsigned int addr, int ftype, num_t value)
{
    FIELDTYPE ft = static_cast<FIELDTYPE>(ftype);

    if(addr == 0x00B2BEE4)
        return; // sBlockNum follows the engine's blocks

    int index;
    unsigned offset;
    size_t table;
    if(memTypeArrayAt(addr, table, index, offset))
    {
        memTypeArraySet(table, index, offset, ft, (double)value);
        return;
    }

    if(const ObjMemArray *a = objMemArrayAt(addr, index, offset))
    {
        int k;
        if(const MemField *f = memFieldAt(*a, offset, k))
        {
            if(f->set)
                f->set(index, k, (double)value);
            return;
        }

        switch(a->kind)
        {
        case OBJMEM_FIELDS:
            break;
        case OBJMEM_PLAYER:
            MemAssign(&Player[index], offset, value, OP_Assign, ft);
            return;
        case OBJMEM_NPC:
            if(offset == 0xE2 || offset == 0xDC) // Type, DefaultType
                value = memEngineType(value, maxNPCType, "NPC");
            MemAssign(&NPC[index], offset, value, OP_Assign, ft);
            return;
        case OBJMEM_BLOCK:
            if(offset == 0x1E || offset == 0x06) // Type, DefaultType
                value = memEngineType(value, maxBlockType, "Block");
            MemAssign(&Block[index], offset, value, OP_Assign, ft);
            return;
        case OBJMEM_BGO:
            if(offset == 0x06) // Type
                value = memEngineType(value, maxBackgroundType, "BGO");
            MemAssign(&Background[index], offset, value, OP_Assign, ft);
            return;
        case OBJMEM_TEMPLATE:
            MemAssign(&s_templatePlayers[index], offset, value, OP_Assign, ft);
            return;
        }

        mem_unknown("write", addr);
        return;
    }

    if(addr >= GM_BASE && addr < GM_END)
    {
        MemAssign(addr, value, OP_Assign, ft);
        return;
    }

    mem_unknown("write", addr);
}

// smbx2compat: mem() reads for LuaJIT's FFI (ffi_mem.lua calls this through a
// function pointer, __xtech_ffiMemGet). LuaJIT can compile FFI calls into its
// traces, but not luabind calls, and SMBX2 reads ~17,000 fields a frame in
// SMATRS 1-1. Reads only: they never call back into Lua, which an FFI-called
// function must not do. Writes are rare and stay on luabind.
static double ffi_mem_get(uint32_t addr, int32_t ftype)
{
    return mem_get(addr, ftype);
}

// SMBX2's mem(addr, FIELD_STRING): VB6 strings in objects and globals
static std::string mem_getString(unsigned int addr)
{
    int index, k;
    unsigned offset;
    std::string ret;
    if(const ObjMemArray *a = objMemArrayAt(addr, index, offset))
    {
        const MemField *f = memFieldAt(*a, offset, k);
        if(f && f->getS)
            return f->getS(index, k);
    }
    else if(GetMemString(addr, ret))
        return ret;

    mem_unknown("string read", addr);
    return ret;
}

static void mem_setString(unsigned int addr, const std::string &value)
{
    int index, k;
    unsigned offset;
    if(const ObjMemArray *a = objMemArrayAt(addr, index, offset))
    {
        const MemField *f = memFieldAt(*a, offset, k);
        if(f && f->setS)
        {
            f->setS(index, k, value);
            return;
        }
    }
    else if(SetMemString(addr, value))
        return;

    mem_unknown("string write", addr);
}

// smbx2compat: SMBX2's Block.iterateIntersecting/getIntersecting (patched in
// core/patches.lua). SMBX2 scans the blocks its X-sorted column lookup points at;
// we ask the engine's block quadtree instead, then apply SMBX2's own test (edges
// that only touch don't count). Returns block indices, lowest first.
static luabind::object block_queryIntersecting(double x1, double y1, double x2, double y2)
{
    lua_State* L = xtech_lua_getState();
    luabind::object ret = luabind::newtable(L);

    num_t left(std::min(x1, x2), nullptr);
    num_t top(std::min(y1, y2), nullptr);
    num_t right(std::max(x1, x2), nullptr);
    num_t bottom(std::max(y1, y2), nullptr);

    int n = 1;
    for(BlockRef_t b : treeFLBlockQuery(left, top, right, bottom, SORTMODE_ID))
    {
        int i = b;
        if(i < 1 || i > numBlock)
            continue;

        const Location_t &loc = Block[i].Location;
        double bx = (double)loc.X;
        double by = (double)loc.Y;
        if(x2 > bx && y2 > by && bx + (double)loc.Width > x1 && by + (double)loc.Height > y1)
            ret[n++] = i;
    }

    return ret;
}

// smbx2compat: LunaLua's PlayerSettings (CustomGraphics.h): hitbox, duck height
// and grab spot per character and powerup, and sprite offsets per frame.
// Character and powerup are checked in Lua (core/classes.lua).
static int *playerSettingsField(int character, int state, const std::string &field)
{
    if(character < 1 || character > numCharacters || state < 1 || state > numStates)
        return nullptr;
    if(field == "hitboxWidth")
        return &Physics.PlayerWidth[character][state];
    if(field == "hitboxHeight")
        return &Physics.PlayerHeight[character][state];
    if(field == "hitboxDuckHeight")
        return &Physics.PlayerDuckHeight[character][state];
    if(field == "grabOffsetX")
        return &Physics.PlayerGrabSpotX[character][state];
    if(field == "grabOffsetY")
        return &Physics.PlayerGrabSpotY[character][state];
    return nullptr;
}

static int playerSettings_get(int character, int state, const std::string &field)
{
    int *v = playerSettingsField(character, state, field);
    return v ? *v : 0;
}

static void playerSettings_set(int character, int state, const std::string &field, int value)
{
    if(int *v = playerSettingsField(character, state, field))
        *v = value;
}

// Sprite offset for the frame at (indexX, indexY) of the sprite sheet's 10x10
// grid; same sign as SMBX's MarioFrameX. Returns false for a bad index.
static bool playerSettingsFrame(int character, int state, int indexX, int indexY, int &frame)
{
    if(character < 1 || character > numCharacters || state < 1 || state > numStates)
        return false;
    if(indexX < 0 || indexY < 0 || indexX > 9 || indexY > 9)
        return false;
    frame = (indexY + 10 * indexX) - 49 + state * 100;
    return true;
}

static luabind::object playerSettings_getSpriteOffset(int character, int state, bool y, int indexX, int indexY)
{
    lua_State* L = xtech_lua_getState();
    int frame;
    if(!playerSettingsFrame(character, state, indexX, indexY, frame))
        return luabind::object(); // nil: invalid index
    return luabind::object(L, (int)(y ? PlayerFrameY[character][frame] : PlayerFrameX[character][frame]));
}

static bool playerSettings_setSpriteOffset(int character, int state, bool y, int indexX, int indexY, int value)
{
    int frame;
    if(!playerSettingsFrame(character, state, indexX, indexY, frame))
        return false;
    if(y)
        PlayerFrameY[character][frame] = (int8_t)value;
    else
        PlayerFrameX[character][frame] = (int8_t)value;
    return true;
}

// Address of the character template for LunaLua's character ID, 0 if none
static unsigned mem_templateAddress(int character)
{
    for(const auto &a : s_objMemArrays)
    {
        if(a.kind == OBJMEM_TEMPLATE && character >= a.first && character <= a.last)
            return a.base + a.size * (unsigned)(character - a.first);
    }
    return 0;
}

} // namespace LuaMisc


// Process delayed calls 鈥?called from xtech_lua_loop()
void xtech_lua_process_delayed_calls()
{
    if(LuaMisc::g_delayedCalls.empty())
        return;

    for(auto it = LuaMisc::g_delayedCalls.begin(); it != LuaMisc::g_delayedCalls.end(); )
    {
        it->framesLeft--;
        if(it->framesLeft <= 0)
        {
            try
            {
                it->callback();
            }
            catch(const luabind::error &e)
            {
                // smbx2compat: luabind::error already popped the message
                pLogWarning("Lua error in delayed call: %s", e.what());
            }
            catch(const std::exception &e)
            {
                pLogWarning("Lua exception in delayed call: %s", e.what());
            }
            it = LuaMisc::g_delayedCalls.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void xtech_lua_clear_delayed_calls()
{
    LuaMisc::g_delayedCalls.clear();
}


// ============================================================================
// Sprite API wrappers
// ============================================================================

namespace LuaSprite
{

// Resolve an image path for HUD use (level custom dir first, then episode dir)
static std::string resolveHudImagePath(const std::string &filename)
{
    std::string path = AutocodeManager::resolveCustomFileCase(filename);
    if(!path.empty())
        return path;
    return AutocodeManager::resolveWorldFileCase(filename);
}

static void loadImage(const std::string &filename, double resourceCode, uint32_t transColor)
{
    int code = static_cast<int>(resourceCode);
    if(code == 0)
        return;

    // Load via game's native system for showImage
    std::string path = resolveHudImagePath(filename);
    if(!path.empty())
    {
        auto it = s_hudTextures.find(code);
        if(it != s_hudTextures.end())
            it->second.reset();
        XRender::LoadPicture(s_hudTextures[code], path);
        // Apply transparent color for BMP/JPG (PNG uses alpha channel)
        if(Files::hasSuffix(path, ".jpg") || Files::hasSuffix(path, ".bmp"))
            XRender::setTransparentColor(s_hudTextures[code], transColor);
    }

    // Also load via LunaRender for backward compat (sprites etc.)
    Renderer::Get().LoadBitmapResource(filename, code, transColor);
}

static void loadImageSimple(const std::string &filename, double resourceCode)
{
    int code = static_cast<int>(resourceCode);
    if(code == 0)
        return;

    // Load via game's native system for showImage
    std::string path = resolveHudImagePath(filename);
    if(!path.empty())
    {
        auto it = s_hudTextures.find(code);
        if(it != s_hudTextures.end())
            it->second.reset();
        XRender::LoadPicture(s_hudTextures[code], path);
    }

    // Also load via LunaRender for backward compat (sprites etc.)
    Renderer::Get().LoadBitmapResource(filename, code);
}

static LunaImage* getImage(double resourceCode)
{
    return Renderer::Get().GetImageForResourceCode(static_cast<int>(resourceCode));
}

static bool deleteImage(double resourceCode)
{
    int code = static_cast<int>(resourceCode);

    // Clear from HUD texture map
    auto it = s_hudTextures.find(code);
    if(it != s_hudTextures.end())
    {
        it->second.reset();
        s_hudTextures.erase(it);
    }

    // Also clear from LunaRender
    return Renderer::Get().DeleteImage(code);
}

// Place a sprite into the world
static CSprite* placeSprite(double type, double imgResourceCode, double x, double y, double lifetime)
{
    CSpriteRequest req;
    req.type = static_cast<int>(type);
    req.img_resource_code = static_cast<int>(imgResourceCode);
    req.x = static_cast<int>(x);
    req.y = static_cast<int>(y);
    req.time = static_cast<int>(lifetime);
    gSpriteMan.InstantiateSprite(&req, true);
    // Return the most recently added sprite
    if(!gSpriteMan.m_SpriteList.empty())
        return gSpriteMan.m_SpriteList.back();
    return nullptr;
}

static CSprite* placeSpriteExt(double type, double imgResourceCode, double x, double y, double lifetime,
    num_t xSpeed, num_t ySpeed, bool spawned)
{
    CSpriteRequest req;
    req.type = static_cast<int>(type);
    req.img_resource_code = static_cast<int>(imgResourceCode);
    req.x = static_cast<int>(x);
    req.y = static_cast<int>(y);
    req.time = static_cast<int>(lifetime);
    req.x_speed = xSpeed;
    req.y_speed = ySpeed;
    req.spawned = spawned;
    gSpriteMan.InstantiateSprite(&req, true);
    if(!gSpriteMan.m_SpriteList.empty())
        return gSpriteMan.m_SpriteList.back();
    return nullptr;
}

static int spriteCount()
{
    return gSpriteMan.CountSprites();
}

static void clearAllSprites()
{
    gSpriteMan.ClearAllSprites();
}

static void clearSpritesByCode(double imgResourceCode)
{
    gSpriteMan.ClearSprites(static_cast<int>(imgResourceCode));
}

static void addBlueprint(const std::string &name, CSprite *spr)
{
    if(spr)
        gSpriteMan.AddBlueprint(name.c_str(), spr);
}

static CSprite* copyFromBlueprint(const std::string &name)
{
    return gSpriteMan.CopyFromBlueprint(name.c_str());
}

} // namespace LuaSprite


// ============================================================================
// Main binding registration
// ============================================================================

// Helper macro to define an NPC ID constant in luabind
// (disabled: GCC 12 + luabind has call_types<int> bug; using raw C API instead)

// Helper macro to define an SFX constant in luabind

// Helper macro to define an EFFID constant in luabind



// smbx2compat: SMBX2's Misc.groupsCollide flips one pair of collision groups;
// smbx2compat/core/lunadll.lua mirrors each change into the engine's matrix
static void lua_collision_setFlipped(unsigned i, unsigned j, bool flipped)
{
    SMBX2Collision_SetFlipped(i, j, flipped);
}

// smbx2compat: SMBX2 indexes LunaLua's extended fields arrays by object index.
// NPCs and blocks carry theirs inside NPC_t/Block_t (so they move with the
// object); lunadll.lua declares a struct that spans one whole object, with the
// fields at this offset, so the arrays step by the object's size.
template<class T>
static void pushObjectExtLayout(lua_State *L, const char *name, T &first, const void *ext, size_t extSize)
{
    lua_createtable(L, 0, 4);
    lua_pushlightuserdata(L, static_cast<void *>(&first));
    lua_setfield(L, -2, "base");
    lua_pushinteger(L, static_cast<lua_Integer>(static_cast<const char *>(ext) - reinterpret_cast<const char *>(&first)));
    lua_setfield(L, -2, "offset");
    lua_pushinteger(L, static_cast<lua_Integer>(extSize));
    lua_setfield(L, -2, "size");
    lua_pushinteger(L, static_cast<lua_Integer>(sizeof(T)));
    lua_setfield(L, -2, "stride");
    lua_setfield(L, -2, name);
}

static void pushExtendedFieldsLayout(lua_State *L)
{
    lua_createtable(L, 0, 4);
    pushObjectExtLayout(L, "npc", NPC[0], &NPC[0].smbx2Ext, sizeof(SMBX2NPCExt_t));
    pushObjectExtLayout(L, "block", Block[0], &Block[0].smbx2Ext, sizeof(SMBX2BlockExt_t));
    lua_pushlightuserdata(L, static_cast<void *>(g_smbx2PlayerExt));
    lua_setfield(L, -2, "player");
    lua_pushinteger(L, c_smbx2PlayerExtSlots);
    lua_setfield(L, -2, "playerSlots");
    lua_setglobal(L, "__xtech_extendedFields");
}

void xtech_lua_register_bindings(lua_State *L)
{
    using namespace luabind;

    module(L)
    [
        // ================================================================
        // Location_t class binding
        // ================================================================
        class_<Location_t>("Location")
            .def(constructor<>())
            .def_readwrite("X", &Location_t::X)
            .def_readwrite("Y", &Location_t::Y)
            .def_readwrite("Width", &Location_t::Width)
            .def_readwrite("Height", &Location_t::Height)
            .def_readwrite("SpeedX", &Location_t::SpeedX)
            .def_readwrite("SpeedY", &Location_t::SpeedY)
            .def("luaX", &Location_t::luaX)
            .def("luaY", &Location_t::luaY),

        // ================================================================
        // LunaRect struct binding (for sprite rendering rects)
        // ================================================================
        class_<LunaRect>("LunaRect")
            .def(constructor<>())
            .def_readwrite("left", &LunaRect::left)
            .def_readwrite("top", &LunaRect::top)
            .def_readwrite("right", &LunaRect::right)
            .def_readwrite("bottom", &LunaRect::bottom),

        // ================================================================
        // Hitbox struct binding
        // ================================================================
        class_<Hitbox>("Hitbox")
            .def(constructor<>())
            .def_readwrite("Left_off", &Hitbox::Left_off)
            .def_readwrite("Top_off", &Hitbox::Top_off)
            .def_readwrite("W", &Hitbox::W)
            .def_readwrite("H", &Hitbox::H)
            .def_readwrite("CollisionType", &Hitbox::CollisionType),

        // ================================================================
        // SpriteComponent struct binding
        // ================================================================
        class_<SpriteComponent>("SpriteComponent")
            .def_readwrite("data1", &SpriteComponent::data1)
            .def_readwrite("data2", &SpriteComponent::data2)
            .def_readwrite("data3", &SpriteComponent::data3)
            .def_readwrite("data4", &SpriteComponent::data4)
            .def_readwrite("lookup_code", &SpriteComponent::lookup_code)
            .def_readwrite("run_time", &SpriteComponent::run_time)
            .def_readwrite("org_time", &SpriteComponent::org_time)
            .def_readwrite("data5", &SpriteComponent::data5)
            .def_readwrite("expired", &SpriteComponent::expired),

        // ================================================================
        // LunaImage class binding (loaded image resource)
        // ================================================================
        class_<LunaImage>("LunaImage")
            .def("getWidth", &LunaImage::getW)
            .def("getHeight", &LunaImage::getH)
            .def("getUID", &LunaImage::getUID)
            .def("isLoaded", &LunaImage::ImageLoaded),

        // ================================================================
        // CSprite class binding
        // ================================================================
        class_<CSprite>("CSprite")
            .def_readwrite("ImgResCode", &CSprite::m_ImgResCode)
            .def_readwrite("CollisionCode", &CSprite::m_CollisionCode)
            .def_readwrite("FramesLeft", &CSprite::m_FramesLeft)
            .def_readwrite("DrawPriorityLevel", &CSprite::m_DrawPriorityLevel)
            .def_readwrite("OffscreenCount", &CSprite::m_OffscreenCount)
            .def_readwrite("FrameCounter", &CSprite::m_FrameCounter)
            .def_readwrite("GfxXOffset", &CSprite::m_GfxXOffset)
            .def_readwrite("GfxYOffset", &CSprite::m_GfxYOffset)
            .def_readwrite("StaticScreenPos", &CSprite::m_StaticScreenPos)
            .def_readwrite("Visible", &CSprite::m_Visible)
            .def_readwrite("Birthed", &CSprite::m_Birthed)
            .def_readwrite("Died", &CSprite::m_Died)
            .def_readwrite("Invalidated", &CSprite::m_Invalidated)
            .def_readwrite("LimitedFrameLife", &CSprite::m_LimitedFrameLife)
            .def_readwrite("AnimationSet", &CSprite::m_AnimationSet)
            .def_readwrite("AlwaysProcess", &CSprite::m_AlwaysProcess)
            .def_readwrite("Xpos", &CSprite::m_Xpos)
            .def_readwrite("Ypos", &CSprite::m_Ypos)
            .def_readwrite("Ht", &CSprite::m_Ht)
            .def_readwrite("Wd", &CSprite::m_Wd)
            .def_readwrite("Xspd", &CSprite::m_Xspd)
            .def_readwrite("Yspd", &CSprite::m_Yspd)
            .def_readwrite("Hitbox", &CSprite::m_Hitbox)
            .def_readwrite("AnimationPhase", &CSprite::m_AnimationPhase)
            .def_readwrite("AnimationTimer", &CSprite::m_AnimationTimer)
            .def_readwrite("AnimationFrame", &CSprite::m_AnimationFrame)
            .def("getExtX", &CSprite::GetExtX)
            .def("getExtY", &CSprite::GetExtY)
            .def("getFrameCols", &CSprite::GetFrameCols)
            .def("setImage", &CSprite::SetImage)
            .def("setImageResource", &CSprite::SetImageResource)
            .def("makeLimitedLife", &CSprite::MakeLimitedLifetime)
            .def("setCustomVar", &CSprite::SetCustomVar)
            .def("customVarExists", &CSprite::CustomVarExists)
            .def("getCustomVar", &CSprite::GetCustomVar)
            .def("birth", &CSprite::Birth)
            .def("die", &CSprite::Die),

        // ================================================================
        // NPC_t class binding (read/write access to key fields)
        // ================================================================
        class_<NPC_t>("NPC")
            .def_readwrite("Type", &NPC_t::Type)
            .def_readwrite("Killed", &NPC_t::Killed)
            .def_readwrite("Frame", &NPC_t::Frame)
            .def_readwrite("Active", &NPC_t::Active)
            .def_readwrite("Hidden", &NPC_t::Hidden)
            .def_readwrite("Inert", &NPC_t::Inert)
            .def_readwrite("Location", &NPC_t::Location)
            .def_readwrite("SpecialX", &NPC_t::SpecialX)
            .def_readwrite("SpecialY", &NPC_t::SpecialY)
            .def_readwrite("Special", &NPC_t::Special)
            .def_readwrite("Special2", &NPC_t::Special2)
            .def_readwrite("Special3", &NPC_t::Special3)
            .def_readwrite("Special4", &NPC_t::Special4)
            .def_readwrite("Special5", &NPC_t::Special5)
            .def_readwrite("Section", &NPC_t::Section)
            .def_readwrite("Wet", &NPC_t::Wet)
            .def_readwrite("Direction", &NPC_t::Direction)
            .def_readwrite("Damage", &NPC_t::Damage)
            .def_readwrite("Immune", &NPC_t::Immune)
            .def_readwrite("CantHurt", &NPC_t::CantHurt)
            .def_readwrite("Multiplier", &NPC_t::Multiplier)
            .def_readwrite("TailCD", &NPC_t::TailCD)
            .def_readwrite("Projectile", &NPC_t::Projectile)
            .def_readwrite("Variant", &NPC_t::Variant)
            .def_readwrite("BattleOwner", &NPC_t::BattleOwner)
            .def_readwrite("HoldingPlayer", &NPC_t::HoldingPlayer)
            .def_readwrite("CantHurtPlayer", &NPC_t::CantHurtPlayer)
            .def_readwrite("RealSpeedX", &NPC_t::RealSpeedX)
            .def_readwrite("BeltSpeed", &NPC_t::BeltSpeed)
            .def_readwrite("oldAddBelt", &NPC_t::oldAddBelt)
            .def_readwrite("Effect", &NPC_t::Effect)
            .def_readwrite("Effect2", &NPC_t::Effect2)
            .def_readwrite("Effect3", &NPC_t::Effect3)
            .def_readwrite("TimeLeft", &NPC_t::TimeLeft)
            .def_readwrite("JustActivated", &NPC_t::JustActivated)
            .def_readwrite("Slope", &NPC_t::Slope)
            .def_readwrite("vehiclePlr", &NPC_t::vehiclePlr)
            .def_readwrite("vehicleYOffset", &NPC_t::vehicleYOffset)
            .def_readwrite("WallDeath", &NPC_t::WallDeath)
            .def_readwrite("GFXSlot", &NPC_t::GFXSlot)
            .def_readwrite("Wings", &NPC_t::Wings)
            .def_readwrite("FrameCount", &NPC_t::FrameCount)
            // --- Newly added NPC fields (P0) ---
            .def_readwrite("DefaultLocationX", &NPC_t::DefaultLocationX)
            .def_readwrite("DefaultLocationY", &NPC_t::DefaultLocationY)
            .def_readwrite("DefaultType", &NPC_t::DefaultType)
            .def_readwrite("DefaultSpecial", &NPC_t::DefaultSpecial)
            .def_readwrite("DefaultDirection", &NPC_t::DefaultDirection)
            .def_readwrite("DefaultWings", &NPC_t::DefaultWings)
            // Event/string indices (mapped as integers)
            .def_readwrite("TriggerActivate", &NPC_t::TriggerActivate)
            .def_readwrite("TriggerDeath", &NPC_t::TriggerDeath)
            .def_readwrite("TriggerTalk", &NPC_t::TriggerTalk)
            .def_readwrite("TriggerLast", &NPC_t::TriggerLast)
            .def_readwrite("Text", &NPC_t::Text)
            .def_readwrite("Layer", &NPC_t::Layer)
            .def_readwrite("AttLayer", &NPC_t::AttLayer)
            .def_readwrite("extx", &NPC_t::extx)
            .def_readwrite("exty", &NPC_t::exty)
            // Bitfield members via getter/setter
            .def("getGenerator", &LuaNPCExt::npc_getGenerator)
            .def("setGenerator", &LuaNPCExt::npc_setGenerator)
            .def("getGeneratorActive", &LuaNPCExt::npc_getGeneratorActive)
            .def("setGeneratorActive", &LuaNPCExt::npc_setGeneratorActive)
            .def("getChat", &LuaNPCExt::npc_getChat)
            .def("setChat", &LuaNPCExt::npc_setChat)
            .def("getLegacy", &LuaNPCExt::npc_getLegacy)
            .def("setLegacy", &LuaNPCExt::npc_setLegacy)
            .def("getTurnAround", &LuaNPCExt::npc_getTurnAround)
            .def("setTurnAround", &LuaNPCExt::npc_setTurnAround)
            .def("getTurnBackWipe", &LuaNPCExt::npc_getTurnBackWipe)
            .def("setTurnBackWipe", &LuaNPCExt::npc_setTurnBackWipe)
            .def("getPlayerTemp", &LuaNPCExt::npc_getplayerTemp)
            .def("setPlayerTemp", &LuaNPCExt::npc_setplayerTemp)
            .def("getNoLavaSplash", &LuaNPCExt::npc_getNoLavaSplash)
            .def("setNoLavaSplash", &LuaNPCExt::npc_setNoLavaSplash)
            .def("getBouce", &LuaNPCExt::npc_getBouce)
            .def("setBouce", &LuaNPCExt::npc_setBouce)
            .def("getDefaultStuck", &LuaNPCExt::npc_getDefaultStuck)
            .def("setDefaultStuck", &LuaNPCExt::npc_setDefaultStuck)
            .def("getRespawnDelay", &LuaNPCExt::npc_getRespawnDelay)
            .def("setRespawnDelay", &LuaNPCExt::npc_setRespawnDelay)
            .def("getStuck", &LuaNPCExt::npc_getStuck)
            .def("setStuck", &LuaNPCExt::npc_setStuck)
            .def("getShadow", &LuaNPCExt::npc_getShadow)
            .def("setShadow", &LuaNPCExt::npc_setShadow)
            .def("getQuicksand", &LuaNPCExt::npc_getQuicksand)
            .def("setQuicksand", &LuaNPCExt::npc_setQuicksand)
            // Generator sub-fields
            .def("getGeneratorDirection", &LuaNPCExt::npc_getGeneratorDirection)
            .def("getGeneratorEffect", &LuaNPCExt::npc_getGeneratorEffect)
            .def("getGeneratorTimeMax", &LuaNPCExt::npc_getGeneratorTimeMax)
            .def("setGeneratorTimeMax", &LuaNPCExt::npc_setGeneratorTimeMax)
            .def("getGeneratorTime", &LuaNPCExt::npc_getGeneratorTime)
            .def("setGeneratorTime", &LuaNPCExt::npc_setGeneratorTime)
            .def("getPermID", &LuaNPCExt::npc_getPermID)
            .def("getName", &LuaNPCExt::npc_getName)
            .def("setName", &LuaNPCExt::npc_setName)
            .def("luaX", +[](NPC_t *n) -> lua_Integer { return (lua_Integer)(n->Location.X.i >> 32); })
            .def("luaY", +[](NPC_t *n) -> lua_Integer { return (lua_Integer)(n->Location.Y.i >> 32); }),

        // ================================================================
        // Player_t class binding (read/write access to all fields)
        // ================================================================
        class_<Player_t>("Player")
            .def_readwrite("DoubleJump", &Player_t::DoubleJump)
            .def_readwrite("FlySparks", &Player_t::FlySparks)
            .def_readwrite("Driving", &Player_t::Driving)
            .def_readwrite("Quicksand", &Player_t::Quicksand)
            .def_readwrite("Bombs", &Player_t::Bombs)
            .def_readwrite("Slippy", &Player_t::Slippy)
            .def_readwrite("Fairy", &Player_t::Fairy)
            .def_readwrite("FairyCD", &Player_t::FairyCD)
            .def_readwrite("FairyTime", &Player_t::FairyTime)
            .def_readwrite("HasKey", &Player_t::HasKey)
            .def_readwrite("Hearts", &Player_t::Hearts)
            .def_readwrite("CanFloat", &Player_t::CanFloat)
            .def_readwrite("FloatRelease", &Player_t::FloatRelease)
            .def_readwrite("FloatTime", &Player_t::FloatTime)
            .def_readwrite("FloatSpeed", &Player_t::FloatSpeed)
            .def_readwrite("FloatDir", &Player_t::FloatDir)
            .def_readwrite("SwordPoke", &Player_t::SwordPoke)
            .def_readwrite("GrabTime", &Player_t::GrabTime)
            .def_readwrite("GrabSpeed", &Player_t::GrabSpeed)
            .def_readwrite("VineNPC", &Player_t::VineNPC)
            .def_readwrite("VineBGO", &Player_t::VineBGO)
            .def_readwrite("Wet", &Player_t::Wet)
            .def_readwrite("WetFrame", &Player_t::WetFrame)
            .def_readwrite("SwimCount", &Player_t::SwimCount)
            .def_readwrite("NoGravity", &Player_t::NoGravity)
            .def_readwrite("Slide", &Player_t::Slide)
            .def_readwrite("SlideKill", &Player_t::SlideKill)
            .def_readwrite("Vine", &Player_t::Vine)
            .def_readwrite("ShellSurf", &Player_t::ShellSurf)
            .def_readwrite("Rolling", &Player_t::Rolling)
            .def_readwrite("StateNPC", &Player_t::StateNPC)
            .def_readwrite("Slope", &Player_t::Slope)
            .def_readwrite("Stoned", &Player_t::Stoned)
            .def_readwrite("AquaticSwim", &Player_t::AquaticSwim)
            .def_readwrite("StonedCD", &Player_t::StonedCD)
            .def_readwrite("StonedTime", &Player_t::StonedTime)
            .def_readwrite("SpinJump", &Player_t::SpinJump)
            .def_readwrite("SpinFrame", &Player_t::SpinFrame)
            .def_readwrite("SpinFireDir", &Player_t::SpinFireDir)
            .def_readwrite("Multiplier", &Player_t::Multiplier)
            .def_readwrite("SlideCounter", &Player_t::SlideCounter)
            .def_readwrite("ShowWarp", &Player_t::ShowWarp)
            .def_readwrite("ForceHold", &Player_t::ForceHold)
            .def_readwrite("YoshiYellow", &Player_t::YoshiYellow)
            .def_readwrite("YoshiBlue", &Player_t::YoshiBlue)
            .def_readwrite("YoshiRed", &Player_t::YoshiRed)
            .def_readwrite("YoshiWingsFrame", &Player_t::YoshiWingsFrame)
            .def_readwrite("YoshiWingsFrameCount", &Player_t::YoshiWingsFrameCount)
            .def_readwrite("YoshiTX", &Player_t::YoshiTX)
            .def_readwrite("YoshiTY", &Player_t::YoshiTY)
            .def_readwrite("YoshiTFrame", &Player_t::YoshiTFrame)
            .def_readwrite("YoshiTFrameCount", &Player_t::YoshiTFrameCount)
            .def_readwrite("YoshiBX", &Player_t::YoshiBX)
            .def_readwrite("YoshiBY", &Player_t::YoshiBY)
            .def_readwrite("YoshiBFrame", &Player_t::YoshiBFrame)
            .def_readwrite("YoshiBFrameCount", &Player_t::YoshiBFrameCount)
            .def_readwrite("YoshiTongue", &Player_t::YoshiTongue)
            .def_readwrite("YoshiTonugeBool", &Player_t::YoshiTonugeBool)
            .def_readwrite("YoshiTongueLength", &Player_t::YoshiTongueLength)
            .def_readwrite("YoshiNPC", &Player_t::YoshiNPC)
            .def_readwrite("YoshiPlayer", &Player_t::YoshiPlayer)
            .def_readwrite("Dismount", &Player_t::Dismount)
            .def_readwrite("Location", &Player_t::Location)
            .def_readwrite("Character", &Player_t::Character)
            .def_readwrite("Direction", &Player_t::Direction)
            .def_readwrite("Mount", &Player_t::Mount)
            .def_readwrite("MountType", &Player_t::MountType)
            .def_readwrite("MountSpecial", &Player_t::MountSpecial)
            .def_readwrite("MountOffsetY", &Player_t::MountOffsetY)
            .def_readwrite("MountFrame", &Player_t::MountFrame)
            .def_readwrite("State", &Player_t::State)
            .def_readwrite("Frame", &Player_t::Frame)
            .def_readwrite("FrameCount", &Player_t::FrameCount)
            .def_readwrite("Jump", &Player_t::Jump)
            .def_readwrite("CanJump", &Player_t::CanJump)
            .def_readwrite("CanAltJump", &Player_t::CanAltJump)
            .def_readwrite("Effect", &Player_t::Effect)
            .def_readwrite("Effect2", &Player_t::Effect2)
            .def_readwrite("RespawnY", &Player_t::RespawnY)
            .def_readwrite("Duck", &Player_t::Duck)
            .def_readwrite("DropRelease", &Player_t::DropRelease)
            .def_readwrite("StandUp", &Player_t::StandUp)
            .def_readwrite("StandUp2", &Player_t::StandUp2)
            .def_readwrite("Bumped", &Player_t::Bumped)
            // --- Newly added Player_t fields (high priority) ---
            .def_readwrite("Bumped2", &Player_t::Bumped2)
            .def_readwrite("Dead", &Player_t::Dead)
            .def_readwrite("TimeToLive", &Player_t::TimeToLive)
            .def_readwrite("Immune", &Player_t::Immune)
            .def_readwrite("Immune2", &Player_t::Immune2)
            .def_readwrite("ForceHitSpot3", &Player_t::ForceHitSpot3)
            .def_readwrite("HoldingNPC", &Player_t::HoldingNPC)
            .def_readwrite("CanGrabNPCs", &Player_t::CanGrabNPCs)
            .def_readwrite("HeldBonus", &Player_t::HeldBonus)
            .def_readwrite("Section", &Player_t::Section)
            .def_readwrite("WarpCD", &Player_t::WarpCD)
            .def_readwrite("Warp", &Player_t::Warp)
            .def_readwrite("WarpBackward", &Player_t::WarpBackward)
            .def_readwrite("WarpShooted", &Player_t::WarpShooted)
            .def_readwrite("FireBallCD", &Player_t::FireBallCD)
            .def_readwrite("FireBallCD2", &Player_t::FireBallCD2)
            .def_readwrite("TailCount", &Player_t::TailCount)
            .def_readwrite("RunCount", &Player_t::RunCount)
            .def_readwrite("CanFly", &Player_t::CanFly)
            .def_readwrite("CanFly2", &Player_t::CanFly2)
            .def_readwrite("FlyCount", &Player_t::FlyCount)
            .def_readwrite("RunRelease", &Player_t::RunRelease)
            .def_readwrite("JumpRelease", &Player_t::JumpRelease)
            .def_readwrite("StandingOnNPC", &Player_t::StandingOnNPC)
            .def_readwrite("StandingOnVehiclePlr", &Player_t::StandingOnVehiclePlr)
            .def_readwrite("UnStart", &Player_t::UnStart)
            .def_readwrite("mountBump", &Player_t::mountBump)
            .def_readwrite("CurMazeZone", &Player_t::CurMazeZone)
            .def_readwrite("MazeZoneStatus", &Player_t::MazeZoneStatus)
            // Bitfield members via getter/setter
            .def("getGroundPound", &LuaPlayerExt::player_getGroundPound)
            .def("setGroundPound", &LuaPlayerExt::player_setGroundPound)
            .def("getGroundPound2", &LuaPlayerExt::player_getGroundPound2)
            .def("setGroundPound2", &LuaPlayerExt::player_setGroundPound2)
            .def("getCanPound", &LuaPlayerExt::player_getCanPound)
            .def("setCanPound", &LuaPlayerExt::player_setCanPound)
            .def("getAltRunRelease", &LuaPlayerExt::player_getAltRunRelease)
            .def("setAltRunRelease", &LuaPlayerExt::player_setAltRunRelease)
            .def("getDuckRelease", &LuaPlayerExt::player_getDuckRelease)
            .def("setDuckRelease", &LuaPlayerExt::player_setDuckRelease)
            .def("getSlippyWall", &LuaPlayerExt::player_getSlippyWall)
            .def("setSlippyWall", &LuaPlayerExt::player_setSlippyWall)
            .def("getJumpOffWall", &LuaPlayerExt::player_getJumpOffWall)
            .def("setJumpOffWall", &LuaPlayerExt::player_setJumpOffWall)
            .def("luaX", +[](Player_t *p) -> lua_Integer { return (lua_Integer)(p->Location.X.i >> 32); })
            .def("luaY", +[](Player_t *p) -> lua_Integer { return (lua_Integer)(p->Location.Y.i >> 32); }),

        // ================================================================
        // Layer_t class binding
        // ================================================================
        class_<Layer_t>("Layer")
            .def_readonly("Name", &Layer_t::Name)
            .def_readwrite("SpeedX", &Layer_t::SpeedX)
            .def_readwrite("SpeedY", &Layer_t::SpeedY)
            .def_readwrite("Hidden", &Layer_t::Hidden)
            .def_readwrite("EffectStop", &Layer_t::EffectStop),

        // ================================================================
        // Block_t class binding
        // ================================================================
        class_<Block_t>("Block")
            .def_readwrite("Location", &Block_t::Location)
            .def_readwrite("Type", &Block_t::Type)
            .def_readwrite("Special", &Block_t::Special)
            .def_readwrite("Invis", &Block_t::Invis)
            .def_readwrite("Hidden", &Block_t::Hidden)
            .def_readwrite("Slippy", &Block_t::Slippy)
            .def_readwrite("Kill", &Block_t::Kill)
            .def_readwrite("RapidHit", &Block_t::RapidHit)
            // --- Newly added Block fields (P0) ---
            .def_readwrite("forceSmashable", &Block_t::forceSmashable)
            .def_readwrite("RespawnDelay_ScreensLeft", &Block_t::RespawnDelay_ScreensLeft)
            .def_readwrite("DefaultType", &Block_t::DefaultType)
            .def_readwrite("DefaultSpecial", &Block_t::DefaultSpecial)
            .def_readwrite("TriggerHit", &Block_t::TriggerHit)
            .def_readwrite("TriggerDeath", &Block_t::TriggerDeath)
            .def_readwrite("TriggerLast", &Block_t::TriggerLast)
            .def_readwrite("Layer", &Block_t::Layer)
            .def_readwrite("ShakeCounter", &Block_t::ShakeCounter)
            .def_readwrite("ShakeOffset", &Block_t::ShakeOffset)
            .def_readwrite("coinSwitchNpcType", &Block_t::coinSwitchNpcType)
            .def_readwrite("tempBlockVehiclePlr", &Block_t::tempBlockVehiclePlr)
            .def_readwrite("tempBlockNpcType", &Block_t::tempBlockNpcType)
            .def_readwrite("tempBlockVehicleYOffset", &Block_t::tempBlockVehicleYOffset)
            .def_readwrite("tempBlockNpcIdx", &Block_t::tempBlockNpcIdx)
            .def_readwrite("extx", &Block_t::extx)
            .def_readwrite("exty", &Block_t::exty)
            .def("getPermID", &LuaBlock::block_getPermID)
            .def("getName", &LuaBlock::block_getName)
            .def("setName", &LuaBlock::block_setName)
            .def("luaX", +[](Block_t *b) -> lua_Integer { return (lua_Integer)(b->Location.X.i >> 32); })
            .def("luaY", +[](Block_t *b) -> lua_Integer { return (lua_Integer)(b->Location.Y.i >> 32); }),

        // ================================================================
        // SpeedlessLocation_t struct binding
        // (used by Background_t, Water_t, Warp_t, Sections)
        // ================================================================
        class_<SpeedlessLocation_t>("SpeedlessLocation")
            .def(constructor<>())
            .def_readwrite("X", &SpeedlessLocation_t::X)
            .def_readwrite("Y", &SpeedlessLocation_t::Y)
            .def_readwrite("Width", &SpeedlessLocation_t::Width)
            .def_readwrite("Height", &SpeedlessLocation_t::Height)
            .def("luaX", &SpeedlessLocation_t::luaX)
            .def("luaY", &SpeedlessLocation_t::luaY),

        // ================================================================
        // Background_t (BGO) class binding
        // ================================================================
        class_<Background_t>("BGO")
            .def_readwrite("Type", &Background_t::Type)
            .def_readwrite("Hidden", &Background_t::Hidden)
            .def_readwrite("Layer", &Background_t::Layer)
            .def_readwrite("SortPriority", &Background_t::SortPriority)
            .def_readwrite("Location", &Background_t::Location)
            .def_readwrite("extx", &Background_t::extx)
            .def_readwrite("exty", &Background_t::exty)
            .def("getPermID", &LuaBGO::bgo_getPermID),

        // ================================================================
        // Water_t (Liquid) class binding
        // ================================================================
        class_<Water_t>("Liquid")
            .def_readwrite("Location", &Water_t::Location)
            .def_readwrite("Type", &Water_t::Type)
            .def_readwrite("Hidden", &Water_t::Hidden)
            .def_readwrite("Layer", &Water_t::Layer)
            .def("getPermID", &LuaWater::water_getPermID)
            .def("getName", &LuaWater::water_getName)
            .def("setName", &LuaWater::water_setName),

        // ================================================================
        // Warp_t class binding
        // ================================================================
        class_<Warp_t>("Warp")
            .def_readwrite("Entrance", &Warp_t::Entrance)
            .def_readwrite("Exit", &Warp_t::Exit)
            .def_readwrite("Locked", &Warp_t::Locked)
            .def_readwrite("WarpNPC", &Warp_t::WarpNPC)
            .def_readwrite("NoYoshi", &Warp_t::NoYoshi)
            .def_readwrite("Hidden", &Warp_t::Hidden)
            .def_readwrite("Stars", &Warp_t::Stars)
            .def_readwrite("Effect", &Warp_t::Effect)
            .def_readwrite("LevelWarp", &Warp_t::LevelWarp)
            .def_readwrite("LevelEnt", &Warp_t::LevelEnt)
            .def_readwrite("Direction", &Warp_t::Direction)
            .def_readwrite("Direction2", &Warp_t::Direction2)
            .def_readwrite("MapWarp", &Warp_t::MapWarp)
            .def_readwrite("MapX", &Warp_t::MapX)
            .def_readwrite("MapY", &Warp_t::MapY)
            .def_readwrite("curStars", &Warp_t::curStars)
            .def_readwrite("twoWay", &Warp_t::twoWay)
            .def_readwrite("noPrintStars", &Warp_t::noPrintStars)
            .def_readwrite("noEntranceScene", &Warp_t::noEntranceScene)
            .def_readwrite("cannonExit", &Warp_t::cannonExit)
            .def_readwrite("cannonExitSpeed", &Warp_t::cannonExitSpeed)
            .def_readwrite("stoodRequired", &Warp_t::stoodRequired)
            .def_readwrite("eventEnter", &Warp_t::eventEnter)
            .def_readwrite("eventExit", &Warp_t::eventExit)
            .def_readwrite("StarsMsg", &Warp_t::StarsMsg)
            .def_readwrite("transitEffect", &Warp_t::transitEffect)
            .def_readwrite("Layer", &Warp_t::Layer)
            .def("getPermID", &LuaWarp::warp_getPermID),

        // ================================================================
        // Effect_t class binding (P3 - FXCreate equivalent)
        // ================================================================
        class_<Effect_t>("Effect")
            .def_readwrite("Location", &Effect_t::Location)
            .def_readwrite("Type", &Effect_t::Type)
            .def_readwrite("Frame", &Effect_t::Frame)
            .def_readwrite("FrameCount", &Effect_t::FrameCount)
            .def_readwrite("Life", &Effect_t::Life)
            .def_readwrite("NewNpc", &Effect_t::NewNpc)
            .def_readwrite("NewNpcSpecial", &Effect_t::NewNpcSpecial)
            .def_readwrite("Shadow", &Effect_t::Shadow),

        // ================================================================
        // Player API functions
        // ================================================================
        def("xtech_player_get", &LuaPlayer::get),
        def("xtech_player_count", &LuaPlayer::count),
        def("xtech_player_harm", &LuaPlayer::harm),
        def("xtech_player_kill", &LuaPlayer::kill),
        def("xtech_player_filterToBig", &LuaPlayer::filterToBig),
        def("xtech_player_filterToSmall", &LuaPlayer::filterToSmall),
        def("xtech_player_filterToFire", &LuaPlayer::filterToFire),
        def("xtech_player_filterMount", &LuaPlayer::filterMount),
        def("xtech_player_filterReservePowerup", &LuaPlayer::filterReservePowerup),
        def("xtech_player_cycleRight", &LuaPlayer::cycleRight),
        def("xtech_player_cycleLeft", &LuaPlayer::cycleLeft),
        def("xtech_player_infiniteFlying", &LuaPlayer::infiniteFlying),
        def("xtech_player_pressingUp", &LuaPlayer::pressingUp),
        def("xtech_player_pressingDown", &LuaPlayer::pressingDown),
        def("xtech_player_pressingLeft", &LuaPlayer::pressingLeft),
        def("xtech_player_pressingRight", &LuaPlayer::pressingRight),
        def("xtech_player_pressingJump", &LuaPlayer::pressingJump),
        def("xtech_player_pressingRun", &LuaPlayer::pressingRun),
        def("xtech_player_pressingSEL", &LuaPlayer::pressingSEL),
        def("xtech_player_isHoldingSpriteType", &LuaPlayer::isHoldingSpriteType),
        def("xtech_player_usesHearts", &LuaPlayer::usesHearts),
        def("xtech_player_memSet", &LuaPlayer::memSet),
        def("xtech_player_dismount", &LuaPlayer::dismount),

        // ================================================================
        // NPC API functions
        // ================================================================
        def("xtech_npc_get", &LuaNPC::get),
        def("xtech_npc_count", &LuaNPC::count),
        def("xtech_npc_getFirstMatch", &LuaNPC::getFirstMatch),
        def("xtech_npc_memSet", &LuaNPC::memSet),
        def("xtech_npc_allSetHits", &LuaNPC::allSetHits),
        def("xtech_npc_kill", &LuaNPC::kill),
        def("xtech_npc_hurt", &LuaNPC::hurt),
        def("xtech_npc_getByPermID", &LuaNPC::getByPermID),
        def("xtech_npc_getByName", &LuaNPC::npc_getByName),
        def("xtech_npc_forEach", &LuaNPC::forEach),
        def("xtech_npc_create", &LuaNPC::lua_npc_create),
        def("xtech_npc_spawnLegacy", &LuaNPC::lua_npc_spawnLegacy), // smbx2compat
        def("xtech_collision_setFlipped", &lua_collision_setFlipped), // smbx2compat
        def("xtech_collision_clear", &SMBX2Collision_Clear), // smbx2compat

        // ================================================================
        // Layer API functions
        // ================================================================
        def("xtech_layer_get", &LuaLayer::get),
        def("xtech_layer_count", &LuaLayer::count),
        def("xtech_layer_show", &LuaLayer::show), // smbx2compat
        def("xtech_layer_hide", &LuaLayer::hide), // smbx2compat
        def("xtech_layer_setXSpeed", &LuaLayer::setXSpeed),
        def("xtech_layer_setYSpeed", &LuaLayer::setYSpeed),
        def("xtech_layer_stop", &LuaLayer::stop),

        // ================================================================
        // Block API functions
        // ================================================================
        def("xtech_block_get", &LuaBlock::get),
        def("xtech_block_count", &LuaBlock::count),
        def("xtech_block_setAll", &LuaBlock::setAll),
        def("xtech_block_swapAll", &LuaBlock::swapAll),
        def("xtech_block_showAll", &LuaBlock::showAll),
        def("xtech_block_hideAll", &LuaBlock::hideAll),
        def("xtech_block_isPlayerTouchingType", &LuaBlock::isPlayerTouchingType),
        def("xtech_block_getByPermID", &LuaBlock::getByPermID_wrapper),
        def("xtech_block_getByName", &LuaBlock::block_getByName),
        def("xtech_block_forEach", &LuaBlock::forEach),

        // ================================================================
        // BGO API functions
        // ================================================================
        def("xtech_bgo_get", &LuaBGO::get),
        def("xtech_bgo_count", &LuaBGO::count),
        def("xtech_bgo_getByPermID", &LuaBGO::getByPermID),
        def("xtech_bgo_forEach", &LuaBGO::forEach),

        // ================================================================
        // Liquid / Water API functions
        // ================================================================
        def("xtech_liquid_get", &LuaWater::get),
        def("xtech_liquid_count", &LuaWater::count),
        def("xtech_liquid_getByPermID", &LuaWater::getByPermID),
        def("xtech_liquid_getByName", &LuaWater::water_getByName),

        // ================================================================
        // Warp API functions
        // ================================================================
        def("xtech_warp_get", &LuaWarp::get),
        def("xtech_warp_count", &LuaWarp::count),
        def("xtech_warp_getByPermID", &LuaWarp::getByPermID),

        // ================================================================
        // Section API functions
        // ================================================================
        def("xtech_section_get", &LuaSection::get),
        def("xtech_section_count", &LuaSection::count),
        def("xtech_section_getBackground", &LuaSection::getBackground),
        def("xtech_section_setBackground", &LuaSection::setBackground),
        def("xtech_section_getMusic", &LuaSection::getMusic),
        def("xtech_section_setMusic", &LuaSection::setMusic),
        def("xtech_section_getMusicFile", &LuaSection::getMusicFile),
        def("xtech_section_setMusicFile", &LuaSection::setMusicFile),
        def("xtech_section_getOffScreenExit", &LuaSection::getOffScreenExit),
        def("xtech_section_setOffScreenExit", &LuaSection::setOffScreenExit),

        // ================================================================
        // Effect API functions (P3)
        // ================================================================
        def("xtech_effect_get", &LuaEffect::get),
        def("xtech_effect_count", &LuaEffect::count),
        def("xtech_effect_create", &LuaEffect::create),
        def("xtech_effect_kill", &LuaEffect::kill),

        // ================================================================
        // Audio API functions
        // ================================================================
        // Audio — SFX
        def("xtech_audio_playSFX", &LuaAudio::playSFX),
        def("xtech_audio_stopSFX", &LuaAudio::stopSFX),
        def("xtech_audio_playSFXSpatial", &LuaAudio::playSFXSpatial),
        // Audio — SFX external
        def("xtech_audio_playSFXExt", &LuaAudio::playSFXExt),
        def("xtech_audio_stopSFXExt", &LuaAudio::stopSFXExt),
        def("xtech_audio_preloadSFXExt", &LuaAudio::preloadSFXExt),
        def("xtech_audio_stopAllSFX", &LuaAudio::stopAllSFX),
        // Audio — Music
        def("xtech_audio_playMusic", &LuaAudio::playMusic),
        def("xtech_audio_playMusicFile", &LuaAudio::playMusicFile),
        def("xtech_audio_setMusic", &LuaAudio::setMusic),
        def("xtech_audio_stopMusic", &LuaAudio::stopMusic),
        def("xtech_audio_pauseMusic", &LuaAudio::pauseMusic),
        def("xtech_audio_resumeMusic", &LuaAudio::resumeMusic),
        def("xtech_audio_fadeOutMusic", &LuaAudio::fadeOutMusic),
        // Audio — Master volume
        def("xtech_audio_getSFXVolume", &LuaAudio::getSFXVolume),
        def("xtech_audio_setSFXVolume", &LuaAudio::setSFXVolume),
        def("xtech_audio_getMusicVolume", &LuaAudio::getMusicVolume),
        def("xtech_audio_setMusicVolume", &LuaAudio::setMusicVolume),
#ifdef THEXTECH_ENABLE_AUDIO_FX
        // Audio — FX (echo, reverb)
        def("xtech_audio_setEcho", &LuaAudio::setEcho),
        def("xtech_audio_setReverb", &LuaAudio::setReverb),
        def("xtech_audio_clearFX", &LuaAudio::clearFX),
#endif

        // ================================================================
        // HUD / Render API functions
        // ================================================================
        def("xtech_hud_showText", &LuaHUD::showText),
        def("xtech_hud_showLevelName", &LuaHUD::showLevelName),
        def("xtech_hud_showLevelFile", &LuaHUD::showLevelFile),
        def("xtech_hud_debugPrint", &LuaHUD::debugPrint),
        def("xtech_hud_showImage", &LuaHUD::showImage),
        def("xtech_hud_showNPC", &LuaHUD::showNPC),

        // ================================================================
        // Variable API functions
        // ================================================================
        def("xtech_var_get", &LuaVar::getVar),
        def("xtech_var_exists", &LuaVar::varExists),
        def("xtech_var_operation", &LuaVar::varOperation),

        // ================================================================
        // Event API functions
        // ================================================================
        def("xtech_event_trigger", &LuaEvent::trigger),
        def("xtech_event_triggerByName", &LuaEvent::triggerByName),
        def("xtech_event_cancelByName", &LuaEvent::cancelByName),
        def("xtech_event_triggerLevelEvent", &LuaEvent::triggerLevelEvent), // smbx2compat

        // ================================================================
        // Misc API functions
        // ================================================================
        def("xtech_misc_getFrame", &LuaMisc::getFrame),
        def("xtech_misc_getSection", &LuaMisc::getSection),
        def("xtech_misc_showMsg", &LuaMisc::showMsg),
        def("xtech_misc_showMsgInfo", &LuaMisc::showMsgInfo),
        def("xtech_misc_showMsgWarn", &LuaMisc::showMsgWarn),
        def("xtech_misc_cheat", &LuaMisc::cheat),
        def("xtech_misc_log", &LuaMisc::logMsg),
        // World map
        def("xtech_worldmap_getCurrentLevel", &LuaMisc::worldmap_getCurrentLevel),
        def("xtech_worldmap_getLevelScreenPos", &LuaMisc::worldmap_getLevelScreenPos),
        def("xtech_worldmap_getLevelMedalCount", &LuaMisc::worldmap_getLevelMedalCount),
        def("xtech_worldmap_isLevelMedalGotten", &LuaMisc::worldmap_isLevelMedalGotten),
        // Config
        def("xtech_misc_getConfig", &LuaMisc::misc_getConfig),
        def("xtech_misc_setConfig", &LuaMisc::misc_setConfig),
        def("xtech_misc_setConfigStr", &LuaMisc::misc_setConfigStr),
        def("xtech_misc_logWarn", &LuaMisc::logWarn),
        def("xtech_misc_logDebug", &LuaMisc::logDebug),
        def("xtech_misc_resolveFileCI", &LuaMisc::misc_resolveFileCI), // smbx2compat
        def("xtech_misc_clock", &LuaMisc::misc_clock), // smbx2compat
        def("xtech_misc_listDir", &LuaMisc::misc_listDir), // smbx2compat
        def("xtech_misc_setGameData", &LuaMisc::misc_setGameData), // smbx2compat
        def("xtech_misc_getGameData", &LuaMisc::misc_getGameData), // smbx2compat
        def("xtech_misc_customParams", &LuaMisc::misc_customParams), // smbx2compat
        def("xtech_misc_imageSize", &LuaMisc::misc_imageSize), // smbx2compat
        def("xtech_misc_imagePixels", &LuaMisc::misc_imagePixels), // smbx2compat
        def("xtech_mem_get", &LuaMisc::mem_get), // smbx2compat
        def("xtech_mem_set", &LuaMisc::mem_set), // smbx2compat
        def("xtech_mem_templateAddress", &LuaMisc::mem_templateAddress), // smbx2compat
        def("xtech_mem_propertyTableAddress", &LuaMisc::mem_propertyTableAddress), // smbx2compat
        def("xtech_block_queryIntersecting", &LuaMisc::block_queryIntersecting), // smbx2compat
        def("xtech_mem_getString", &LuaMisc::mem_getString), // smbx2compat
        def("xtech_mem_setString", &LuaMisc::mem_setString), // smbx2compat
        def("xtech_playerSettings_get", &LuaMisc::playerSettings_get), // smbx2compat
        def("xtech_playerSettings_set", &LuaMisc::playerSettings_set), // smbx2compat
        def("xtech_playerSettings_getSpriteOffset", &LuaMisc::playerSettings_getSpriteOffset), // smbx2compat
        def("xtech_playerSettings_setSpriteOffset", &LuaMisc::playerSettings_setSpriteOffset), // smbx2compat
        // xtech_misc_wait: registered manually (luabind::object param)
        // P3: Sysval functions
        def("xtech_sysval_getLives", &LuaMisc::sysval_getLives),
        def("xtech_sysval_setLives", &LuaMisc::sysval_setLives),
        def("xtech_sysval_getCoins", &LuaMisc::sysval_getCoins),
        def("xtech_sysval_setCoins", &LuaMisc::sysval_setCoins),
        // Medal save info
        def("xtech_medal_getCount", &LuaMisc::medal_getCount),
        def("xtech_medal_isGotten", &LuaMisc::medal_isGotten),
        // World map medal display
        def("xtech_misc_getShowMedalsOnWorldMap", &LuaMisc::misc_getShowMedalsOnWorldMap),
        def("xtech_misc_setShowMedalsOnWorldMap", &LuaMisc::misc_setShowMedalsOnWorldMap),
        def("xtech_sysval_getScore", &LuaMisc::sysval_getScore),
        def("xtech_sysval_setScore", &LuaMisc::sysval_setScore),
        def("xtech_sysval_getScreenX", &LuaMisc::sysval_getScreenX),
        def("xtech_sysval_getScreenY", &LuaMisc::sysval_getScreenY),
        def("xtech_sysval_getScreenWidth", &LuaMisc::sysval_getScreenWidth),
        def("xtech_sysval_getScreenHeight", &LuaMisc::sysval_getScreenHeight),
        def("xtech_sysval_getScreenTop", &LuaMisc::sysval_getScreenTop),
        def("xtech_sysval_getScreenLeft", &LuaMisc::sysval_getScreenLeft),
        def("xtech_sysval_getScreenTopDraw", &LuaMisc::sysval_getScreenTopDraw),
        def("xtech_sysval_getScreenCenterX", &LuaMisc::sysval_getScreenCenterX),
        def("xtech_sysval_getShowHud", &LuaMisc::sysval_getShowHud),
        def("xtech_sysval_setShowHud", &LuaMisc::sysval_setShowHud),
        def("xtech_sysval_getShowInterface", &LuaMisc::sysval_getShowInterface),
        def("xtech_sysval_setShowInterface", &LuaMisc::sysval_setShowInterface),
        def("xtech_sysval_getBattleMode", &LuaMisc::sysval_getBattleMode),
        def("xtech_sysval_getEndLevel", &LuaMisc::sysval_getEndLevel),
        def("xtech_sysval_setEndLevel", &LuaMisc::sysval_setEndLevel),
        def("xtech_sysval_getLevelMacro", &LuaMisc::sysval_getLevelMacro),
        def("xtech_sysval_setLevelMacro", &LuaMisc::sysval_setLevelMacro),
        def("xtech_sysval_getLevelMacroCounter", &LuaMisc::sysval_getLevelMacroCounter),
        def("xtech_sysval_getLevelBeatCode", &LuaMisc::sysval_getLevelBeatCode),
        def("xtech_sysval_setLevelBeatCode", &LuaMisc::sysval_setLevelBeatCode),
        def("xtech_sysval_getGameTime", &LuaMisc::sysval_getGameTime),
        def("xtech_sysval_getCheckpointCount", &LuaMisc::sysval_getCheckpointCount),
        def("xtech_sysval_getCheckpointId", &LuaMisc::sysval_getCheckpointId),
        // P3: Named timer functions
        // xtech_timer_create: registered manually (luabind::object param)
        def("xtech_timer_cancel", &LuaMisc::lua_timer_cancel),
        def("xtech_timer_clearAll", &LuaMisc::lua_timer_clearAll),

        // Custom character registration
        def("xtech_player_setName", &LuaMisc::player_setName),
        def("xtech_player_reload", &LuaMisc::player_reload),
        def("xtech_player_setPhysics", &LuaMisc::player_setPhysics),

        def("xtech_sysval_getLevelName", &LuaMisc::sysval_getLevelName),
        def("xtech_getStrWidth", (int(*)(const std::string&, double))&LuaMisc::misc_getStrWidth),
        def("xtech_getStrWidth", (int(*)(const std::string&))&LuaMisc::misc_getStrWidth),

        // ================================================================
        // Sprite API functions
        // ================================================================
        def("xtech_sprite_loadImage", &LuaSprite::loadImage),
        def("xtech_sprite_loadImageSimple", &LuaSprite::loadImageSimple),
        def("xtech_sprite_getImage", &LuaSprite::getImage),
        def("xtech_sprite_deleteImage", &LuaSprite::deleteImage),
        def("xtech_sprite_place", &LuaSprite::placeSprite),
        def("xtech_sprite_placeExt", &LuaSprite::placeSpriteExt),
        def("xtech_sprite_count", &LuaSprite::spriteCount),
        def("xtech_sprite_clearAll", &LuaSprite::clearAllSprites),
        def("xtech_sprite_clearByCode", &LuaSprite::clearSpritesByCode),
        def("xtech_sprite_addBlueprint", &LuaSprite::addBlueprint),
        def("xtech_sprite_copyFromBlueprint", &LuaSprite::copyFromBlueprint)

        // Constants registered via raw Lua C API below (GCC 12 compat)
    ];

    // ================================================================
    // Manual registrations for functions with luabind::object params
    // (luabind's def() + GCC 12 have template issues with these)
    // ================================================================
    lua_pushcfunction(L, LuaMisc::lua_misc_wait_raw);
    lua_setglobal(L, "xtech_misc_wait");

    lua_pushcfunction(L, LuaMisc::lua_timer_create_raw);
    lua_setglobal(L, "xtech_timer_create");

    lua_pushcfunction(L, LuaMisc::lua_timer_createEvent_raw);
    lua_setglobal(L, "xtech_timer_createEvent");

    // P4: Event subscription
    lua_pushcfunction(L, xtech_lua_subscribe_event);
    lua_setglobal(L, "xtech_event_subscribe");

    // ================================================================
    // Manual constant registrations: luabind's def() with integer
    // constants triggers call_types<int> which is broken on GCC 12.
    // Use raw Lua C API instead.
    // ================================================================
#define LUA_INT_CONST(name, val) do { lua_pushinteger(L, (int)(val)); lua_setglobal(L, name); } while(0)

    LUA_INT_CONST("CONST_DIR_UP", DIR_UP);
    LUA_INT_CONST("CONST_DIR_RIGHT", DIR_RIGHT);
    LUA_INT_CONST("CONST_DIR_DOWN", DIR_DOWN);
    LUA_INT_CONST("CONST_DIR_LEFT", DIR_LEFT);

    LUA_INT_CONST("CONST_OP_ASSIGN", 0);
    LUA_INT_CONST("CONST_OP_ADD", 1);
    LUA_INT_CONST("CONST_OP_SUB", 2);
    LUA_INT_CONST("CONST_OP_MULT", 3);
    LUA_INT_CONST("CONST_OP_DIV", 4);
    LUA_INT_CONST("CONST_OP_XOR", 5);
    LUA_INT_CONST("CONST_OP_ABS", 6);

    LUA_INT_CONST("CONST_CMPT_EQUALS", 0);
    LUA_INT_CONST("CONST_CMPT_GREATER", 1);
    LUA_INT_CONST("CONST_CMPT_LESS", 2);
    LUA_INT_CONST("CONST_CMPT_NOTEQ", 3);

    LUA_INT_CONST("CONST_PRI_LOW", 0);
    LUA_INT_CONST("CONST_PRI_MID", 1);
    LUA_INT_CONST("CONST_PRI_HIGH", 2);

    LUA_INT_CONST("CONST_PLAYER_SMALL", 1);
    LUA_INT_CONST("CONST_PLAYER_SUPER", 2);
    LUA_INT_CONST("CONST_PLAYER_FIRE", 3);
    LUA_INT_CONST("CONST_PLAYER_RACCOON", 4);
    LUA_INT_CONST("CONST_PLAYER_TANOOKI", 5);
    LUA_INT_CONST("CONST_PLAYER_HAMMER", 6);

    LUA_INT_CONST("CONST_MOUNT_NONE", 0);
    LUA_INT_CONST("CONST_MOUNT_BOOT", 1);
    LUA_INT_CONST("CONST_MOUNT_CLOWN_CAR", 2);
    LUA_INT_CONST("CONST_MOUNT_YOSHI", 3);

    LUA_INT_CONST("CONST_SPRITE_CUSTOM", 0);
    LUA_INT_CONST("CONST_SPRITE_STATIC", 1);
    LUA_INT_CONST("CONST_SPRITE_NORMAL", 2);
    LUA_INT_CONST("CONST_SPRITE_ITEM", 3);
    LUA_INT_CONST("CONST_SPRITE_BAR", 4);
    LUA_INT_CONST("CONST_SPRITE_PHANTO", 5);

    LUA_INT_CONST("CONST_BPAT_BEHAVIOR", 0);
    LUA_INT_CONST("CONST_BPAT_DRAW", 1);
    LUA_INT_CONST("CONST_BPAT_BIRTH", 2);
    LUA_INT_CONST("CONST_BPAT_DEATH", 3);

    // Level Macro types
    LUA_INT_CONST("CONST_LEVELMACRO_OFF", 0);
    LUA_INT_CONST("CONST_LEVELMACRO_CARD_ROULETTE", 1);
    LUA_INT_CONST("CONST_LEVELMACRO_QUESTION_SPHERE", 2);
    LUA_INT_CONST("CONST_LEVELMACRO_KEYHOLE", 3);
    LUA_INT_CONST("CONST_LEVELMACRO_CRYSTAL_BALL", 4);
    LUA_INT_CONST("CONST_LEVELMACRO_GAME_COMPLETE", 5);
    LUA_INT_CONST("CONST_LEVELMACRO_STAR", 6);
    LUA_INT_CONST("CONST_LEVELMACRO_GOAL_TAPE", 7);
    LUA_INT_CONST("CONST_LEVELMACRO_FLAG", 8);

    // Beat codes (world map exit paths)
    LUA_INT_CONST("CONST_BEATCODE_NONE", 0);
    LUA_INT_CONST("CONST_BEATCODE_CARD_ROULETTE", 1);
    LUA_INT_CONST("CONST_BEATCODE_QUESTION_SPHERE", 2);
    LUA_INT_CONST("CONST_BEATCODE_OFFSCREEN", 3);
    LUA_INT_CONST("CONST_BEATCODE_KEYHOLE", 4);
    LUA_INT_CONST("CONST_BEATCODE_CRYSTAL_BALL", 5);
    LUA_INT_CONST("CONST_BEATCODE_WARP", 6);
    LUA_INT_CONST("CONST_BEATCODE_STAR", 7);
    LUA_INT_CONST("CONST_BEATCODE_GOAL_TAPE", 8);
    LUA_INT_CONST("CONST_BEATCODE_FLAG", 9);
    LUA_INT_CONST("CONST_BEATCODE_ALT_FLAG", 10);
    LUA_INT_CONST("CONST_BEATCODE_RESERVED_1", 11);
    LUA_INT_CONST("CONST_BEATCODE_RESERVED_2", 12);
    LUA_INT_CONST("CONST_BEATCODE_RESERVED_3", 13);
    LUA_INT_CONST("CONST_BEATCODE_RESERVED_4", 14);
    LUA_INT_CONST("CONST_BEATCODE_GAME_COMPLETE", 15);

    LUA_INT_CONST("CONST_COLTYPE_NONE", 0);
    LUA_INT_CONST("CONST_COLTYPE_LEFT", 1);
    LUA_INT_CONST("CONST_COLTYPE_RIGHT", 2);
    LUA_INT_CONST("CONST_COLTYPE_TOP", 3);
    LUA_INT_CONST("CONST_COLTYPE_BOT", 4);

    LUA_INT_CONST("CONST_FT_BYTE", (int)FT_BYTE);
    LUA_INT_CONST("CONST_FT_WORD", (int)FT_WORD);
    LUA_INT_CONST("CONST_FT_DWORD", (int)FT_DWORD);
    LUA_INT_CONST("CONST_FT_FLOAT", (int)FT_FLOAT);
    LUA_INT_CONST("CONST_FT_DFLOAT", (int)FT_DFLOAT);

    // Physics constants (P6: exposed for Lua P-meter / speed-based logic)
    LUA_INT_CONST("CONST_PLAYER_RUN_SPEED", Physics.PlayerRunSpeed);

#undef LUA_INT_CONST

    // ================================================================
    // Full constant registrations via raw Lua C API
    // (bypasses luabind def() which is broken with GCC 12 for int/enum)
    // ================================================================
#define R_NPC(id) lua_pushinteger(L, (int)NPCID_##id); lua_setglobal(L, "CONST_NPC_" #id);
#define R_SFX(id) lua_pushinteger(L, (int)SFX_##id); lua_setglobal(L, "CONST_SFX_" #id);
#define R_EFF(id) lua_pushinteger(L, (int)EFFID_##id); lua_setglobal(L, "CONST_EFF_" #id);

    // --- NPC IDs (306 entries) ---
    R_NPC(NULL); R_NPC(FODDER_S3); R_NPC(RED_FODDER); R_NPC(RED_FLY_FODDER);
    R_NPC(GRN_TURTLE_S3); R_NPC(GRN_SHELL_S3); R_NPC(RED_TURTLE_S3); R_NPC(RED_SHELL_S3);
    R_NPC(PLANT_S3); R_NPC(POWER_S3); R_NPC(COIN_S3); R_NPC(ITEMGOAL);
    R_NPC(LAVABUBBLE); R_NPC(PLR_FIREBALL); R_NPC(FIRE_POWER_S3); R_NPC(MINIBOSS);
    R_NPC(GOALORB_S3); R_NPC(BULLET); R_NPC(BIG_BULLET); R_NPC(BLU_GUY);
    R_NPC(RED_GUY); R_NPC(CANNONENEMY); R_NPC(CANNONITEM); R_NPC(GLASS_TURTLE);
    R_NPC(GLASS_SHELL); R_NPC(JUMPER_S3); R_NPC(SPRING); R_NPC(UNDER_FODDER);
    R_NPC(RED_FISH_S1); R_NPC(HEAVY_THROWER); R_NPC(HEAVY_THROWN); R_NPC(KEY);
    R_NPC(COIN_SWITCH); R_NPC(COIN_S4); R_NPC(LEAF_POWER); R_NPC(GRN_BOOT);
    R_NPC(SPIKY_S3); R_NPC(STONE_S3); R_NPC(GHOST_S3); R_NPC(SPIT_BOSS);
    R_NPC(SPIT_BOSS_BALL); R_NPC(GOALORB_S2); R_NPC(GHOST_FAST); R_NPC(GHOST_S4);
    R_NPC(BIG_GHOST); R_NPC(SLIDE_BLOCK); R_NPC(FALL_BLOCK_RED); R_NPC(SPIKY_THROWER);
    R_NPC(SPIKY_BALL_S3); R_NPC(TOOTHYPIPE); R_NPC(TOOTHY); R_NPC(BOTTOM_PLANT);
    R_NPC(SIDE_PLANT); R_NPC(CRAB); R_NPC(FLY); R_NPC(EXT_TURTLE);
    R_NPC(VEHICLE); R_NPC(CONVEYOR); R_NPC(METALBARREL); R_NPC(YELSWITCH_FODDER);
    R_NPC(YEL_PLATFORM); R_NPC(BLUSWITCH_FODDER); R_NPC(BLU_PLATFORM); R_NPC(GRNSWITCH_FODDER);
    R_NPC(GRN_PLATFORM); R_NPC(REDSWITCH_FODDER); R_NPC(RED_PLATFORM); R_NPC(HPIPE_SHORT);
    R_NPC(HPIPE_LONG); R_NPC(VPIPE_SHORT); R_NPC(VPIPE_LONG); R_NPC(BIG_FODDER);
    R_NPC(BIG_TURTLE); R_NPC(BIG_SHELL); R_NPC(BIG_PLANT); R_NPC(CIVILIAN_SCARED);
    R_NPC(GRN_FLY_TURTLE_S3); R_NPC(JUMPER_S4); R_NPC(TANK_TREADS); R_NPC(SHORT_WOOD);
    R_NPC(LONG_WOOD); R_NPC(SLANT_WOOD_L); R_NPC(SLANT_WOOD_R); R_NPC(SLANT_WOOD_M);
    R_NPC(STATUE_S3); R_NPC(STATUE_FIRE); R_NPC(VILLAIN_S3); R_NPC(VILLAIN_FIRE);
    R_NPC(COIN_S1); R_NPC(FODDER_S1); R_NPC(LIFE_S3); R_NPC(ITEM_BURIED);
    R_NPC(VEGGIE_1); R_NPC(PLANT_S1); R_NPC(CIVILIAN); R_NPC(PET_GREEN);
    R_NPC(ITEM_POD); R_NPC(STAR_EXIT); R_NPC(PET_BLUE); R_NPC(PET_YELLOW);
    R_NPC(PET_RED); R_NPC(CHAR2); R_NPC(CHAR5); R_NPC(RED_COIN);
    R_NPC(PLATFORM_S3); R_NPC(CHECKER_PLATFORM); R_NPC(PLATFORM_S1); R_NPC(PINK_CIVILIAN);
    R_NPC(PET_FIRE); R_NPC(GRN_TURTLE_S4); R_NPC(RED_TURTLE_S4); R_NPC(BLU_TURTLE_S4);
    R_NPC(YEL_TURTLE_S4); R_NPC(GRN_SHELL_S4); R_NPC(RED_SHELL_S4); R_NPC(BLU_SHELL_S4);
    R_NPC(YEL_SHELL_S4); R_NPC(GRN_HIT_TURTLE_S4); R_NPC(RED_HIT_TURTLE_S4); R_NPC(BLU_HIT_TURTLE_S4);
    R_NPC(YEL_HIT_TURTLE_S4); R_NPC(GRN_FLY_TURTLE_S4); R_NPC(RED_FLY_TURTLE_S4); R_NPC(BLU_FLY_TURTLE_S4);
    R_NPC(YEL_FLY_TURTLE_S4); R_NPC(KNIGHT); R_NPC(BLU_SLIME); R_NPC(CYAN_SLIME);
    R_NPC(RED_SLIME); R_NPC(BIRD); R_NPC(RED_SPIT_GUY); R_NPC(BLU_SPIT_GUY);
    R_NPC(GRY_SPIT_GUY); R_NPC(SPIT_GUY_BALL); R_NPC(BOMB); R_NPC(WALK_BOMB_S2);
    R_NPC(WALK_BOMB_S3); R_NPC(LIT_BOMB_S3); R_NPC(COIN_S2); R_NPC(VEGGIE_2);
    R_NPC(VEGGIE_3); R_NPC(VEGGIE_4); R_NPC(VEGGIE_5); R_NPC(VEGGIE_6);
    R_NPC(VEGGIE_7); R_NPC(VEGGIE_8); R_NPC(VEGGIE_9); R_NPC(VEGGIE_RANDOM);
    R_NPC(PET_BLACK); R_NPC(PET_PURPLE); R_NPC(PET_PINK); R_NPC(SIGN);
    R_NPC(RING); R_NPC(POISON); R_NPC(CARRY_BLOCK_A); R_NPC(CARRY_BLOCK_B);
    R_NPC(CARRY_BLOCK_C); R_NPC(CARRY_BLOCK_D); R_NPC(CARRY_BUDDY); R_NPC(LIFT_SAND);
    R_NPC(ROCKET_WOOD); R_NPC(RED_FLY_TURTLE_S3); R_NPC(BRUTE); R_NPC(BRUTE_SQUISHED);
    R_NPC(BIG_GUY); R_NPC(CARRY_FODDER); R_NPC(HIT_CARRY_FODDER); R_NPC(FLY_CARRY_FODDER);
    R_NPC(CHASER); R_NPC(STATUE_POWER); R_NPC(HEAVY_POWER); R_NPC(PLR_HEAVY);
    R_NPC(GRN_SHELL_S1); R_NPC(GRN_TURTLE_S1); R_NPC(RED_SHELL_S1); R_NPC(RED_TURTLE_S1);
    R_NPC(GRN_FLY_TURTLE_S1); R_NPC(RED_FLY_TURTLE_S1); R_NPC(AXE); R_NPC(SAW);
    R_NPC(STONE_S4); R_NPC(STATUE_S4); R_NPC(FIRE_POWER_S1); R_NPC(FIRE_POWER_S4);
    R_NPC(POWER_S1); R_NPC(POWER_S4); R_NPC(LIFE_S1); R_NPC(LIFE_S4);
    R_NPC(3_LIFE); R_NPC(SKELETON); R_NPC(RAFT); R_NPC(RED_BOOT);
    R_NPC(CHECKPOINT); R_NPC(BLU_BOOT); R_NPC(RAINBOW_SHELL); R_NPC(FLIPPED_RAINBOW_SHELL);
    R_NPC(STAR_COLLECT); R_NPC(GOALTAPE); R_NPC(CHAR3); R_NPC(LAVA_MONSTER);
    R_NPC(VILLAIN_S1); R_NPC(SICK_BOSS); R_NPC(SICK_BOSS_BALL); R_NPC(FLIER);
    R_NPC(ROCKET_FLIER); R_NPC(WALL_BUG); R_NPC(WALL_SPARK); R_NPC(WALL_TURTLE);
    R_NPC(BOSS_CASE); R_NPC(BOSS_FRAGILE); R_NPC(HOMING_BALL); R_NPC(HOMING_BALL_GEN);
    R_NPC(FALL_BLOCK_BROWN); R_NPC(GRN_VINE_S3); R_NPC(RED_VINE_S3); R_NPC(GRN_VINE_S2);
    R_NPC(YEL_VINE_S2); R_NPC(BLU_VINE_S2); R_NPC(GRN_VINE_BASE_S2); R_NPC(YEL_VINE_BASE_S2);
    R_NPC(BLU_VINE_BASE_S2); R_NPC(LADDER); R_NPC(GRN_VINE_S1); R_NPC(GRN_VINE_TOP_S1);
    R_NPC(GRN_VINE_S4); R_NPC(RED_VINE_TOP_S3); R_NPC(GRN_VINE_TOP_S3); R_NPC(GRN_VINE_TOP_S4);
    R_NPC(PET_CYAN); R_NPC(GRN_FISH_S3); R_NPC(RED_FISH_S3); R_NPC(SQUID_S3);
    R_NPC(GRN_FISH_S4); R_NPC(GRN_FISH_S1); R_NPC(BONE_FISH); R_NPC(SQUID_S1);
    R_NPC(YEL_FISH_S4); R_NPC(ICE_BLOCK); R_NPC(TIME_SWITCH); R_NPC(TNT);
    R_NPC(TIMER_S2); R_NPC(EARTHQUAKE_BLOCK); R_NPC(FODDER_S5); R_NPC(FLY_FODDER_S5);
    R_NPC(FLY_FODDER_S3); R_NPC(FIRE_PLANT); R_NPC(PLANT_FIREBALL); R_NPC(STACKER);
    R_NPC(TIMER_S3); R_NPC(POWER_S2); R_NPC(POWER_S5); R_NPC(GEM_1);
    R_NPC(GEM_5); R_NPC(GEM_20); R_NPC(FLY_POWER); R_NPC(LOCK_DOOR);
    R_NPC(LONG_PLANT_UP); R_NPC(LONG_PLANT_DOWN); R_NPC(COIN_5); R_NPC(FIRE_DISK);
    R_NPC(FIRE_CHAIN); R_NPC(WALK_PLANT); R_NPC(BOMBER_BOSS); R_NPC(ICE_CUBE);
    R_NPC(ICE_POWER_S3); R_NPC(PLR_ICEBALL); R_NPC(SWORDBEAM); R_NPC(MAGIC_BOSS);
    R_NPC(MAGIC_BOSS_SHELL); R_NPC(MAGIC_BOSS_BALL); R_NPC(JUMP_PLANT); R_NPC(BAT);
    R_NPC(VINE_BUG); R_NPC(SWAP_POWER); R_NPC(MEDAL); R_NPC(QUAD_SPITTER);
    R_NPC(QUAD_BALL); R_NPC(ICE_POWER_S4); R_NPC(FLY_BLOCK); R_NPC(FLY_CANNON);
    R_NPC(FIRE_BOSS); R_NPC(FIRE_BOSS_SHELL); R_NPC(FIRE_BOSS_FIRE); R_NPC(ITEM_BUBBLE);
    R_NPC(ITEM_THROWER); R_NPC(SPIKY_S4); R_NPC(SPIKY_BALL_S4); R_NPC(RANDOM_POWER);
    R_NPC(DOOR_MAKER); R_NPC(MAGIC_DOOR); R_NPC(COCKPIT); R_NPC(CHAR3_HEAVY);
    R_NPC(CHAR4_HEAVY); R_NPC(RESERVED_293); R_NPC(RESERVED_294); R_NPC(RESERVED_295);
    R_NPC(RESERVED_296); R_NPC(RESERVED_297); R_NPC(RESERVED_298); R_NPC(RESERVED_299);
    R_NPC(RESERVED_300); R_NPC(INVINCIBILITY_POWER); R_NPC(AQUATIC_POWER); R_NPC(POLAR_POWER);
    R_NPC(CYCLONE_POWER); R_NPC(SHELL_POWER); R_NPC(FLAG_EXIT);

    // --- SFX IDs (106 entries) ---
    R_SFX(Jump); R_SFX(Stomp); R_SFX(BlockHit); R_SFX(BlockSmashed);
    R_SFX(PlayerShrink); R_SFX(PlayerGrow); R_SFX(ItemEmerge); R_SFX(PlayerDied);
    R_SFX(ShellHit); R_SFX(Skid); R_SFX(DropItem); R_SFX(GotItem);
    R_SFX(Camera); R_SFX(Coin); R_SFX(1up); R_SFX(Lava);
    R_SFX(Warp); R_SFX(Fireball); R_SFX(CardRouletteClear); R_SFX(BossBeat);
    R_SFX(DungeonClear); R_SFX(Bullet); R_SFX(Grab); R_SFX(Spring);
    R_SFX(HeavyToss); R_SFX(Slide); R_SFX(NewPath); R_SFX(LevelSelect);
    R_SFX(Do); R_SFX(Pause); R_SFX(Key); R_SFX(PSwitch);
    R_SFX(Whip); R_SFX(Transform); R_SFX(Boot); R_SFX(Smash);
    R_SFX(Stone); R_SFX(SpitBossSpit); R_SFX(SpitBossHit); R_SFX(CrystalBallExit);
    R_SFX(SpitBossBeat); R_SFX(BigFireball); R_SFX(Fireworks); R_SFX(VillainKilled);
    R_SFX(GameBeat); R_SFX(Door); R_SFX(Message); R_SFX(Pet);
    R_SFX(PetHurt); R_SFX(PetTongue); R_SFX(PetBirth); R_SFX(GotStar);
    R_SFX(HeroKill); R_SFX(PlayerDied2); R_SFX(PetSwallow); R_SFX(RingGet);
    R_SFX(Skeleton); R_SFX(Checkpoint); R_SFX(MedalGet); R_SFX(TapeExit);
    R_SFX(LavaMonster); R_SFX(SickBossSpit); R_SFX(SickBossKilled); R_SFX(SMBlockHit);
    R_SFX(SMKilled); R_SFX(SMHurt); R_SFX(SMGlass); R_SFX(SMBossHit);
    R_SFX(SMCry); R_SFX(SMExplosion); R_SFX(Climbing); R_SFX(Swim);
    R_SFX(Grab2); R_SFX(Saw); R_SFX(Throw); R_SFX(PlayerHit);
    R_SFX(HeroStab); R_SFX(HeroHurt); R_SFX(HeroHeart); R_SFX(HeroDied);
    R_SFX(HeroRupee); R_SFX(HeroFire); R_SFX(HeroItem); R_SFX(HeroKey);
    R_SFX(HeroShield); R_SFX(HeroDash); R_SFX(HeroFairy); R_SFX(HeroGrass);
    R_SFX(HeroHit); R_SFX(HeroSwordBeam); R_SFX(Bubble); R_SFX(CoinSwitchTimeout);
    R_SFX(BatFlap); R_SFX(Iceball); R_SFX(Freeze); R_SFX(Icebreak);
    R_SFX(PlayerHeavy); R_SFX(SproutVine); R_SFX(MagicBossShell); R_SFX(MagicBossKilled);
    R_SFX(FireBossKilled); R_SFX(HeroIce); R_SFX(HeroFireRod); R_SFX(FlameThrower);
    R_SFX(FlagExit);

    // --- EFFID (152 entries) ---
    R_EFF(SMOKE_S3_CENTER); R_EFF(BLOCK_SMASH); R_EFF(FODDER_S3_SQUISH); R_EFF(CHAR1_DIE);
    R_EFF(FODDER_S3_DIE); R_EFF(CHAR2_DIE); R_EFF(RED_FODDER_SQUISH); R_EFF(RED_FODDER_DIE);
    R_EFF(GRN_SHELL_S3_DIE); R_EFF(RED_SHELL_S3_DIE); R_EFF(SMOKE_S3); R_EFF(COIN_BLOCK_S3);
    R_EFF(BIG_FIREBALL_TAIL); R_EFF(LAVA_SPLASH); R_EFF(MINIBOSS_DIE); R_EFF(BULLET_DIE);
    R_EFF(BIG_BULLET_DIE); R_EFF(RED_GUY_DIE); R_EFF(BLU_GUY_DIE); R_EFF(GLASS_SHELL_DIE);
    R_EFF(JUMPER_S3_DIE); R_EFF(BLU_BLOCK_SMASH); R_EFF(UNDER_FODDER_DIE); R_EFF(UNDER_FODDER_SQUISH);
    R_EFF(RED_FISH_S1_DIE); R_EFF(HEAVY_THROWER_DIE); R_EFF(GRN_BOOT_DIE); R_EFF(SPIKY_S3_DIE);
    R_EFF(SPIT_BOSS_BALL_DIE); R_EFF(SPIT_BOSS_DIE); R_EFF(SLIDE_BLOCK_SMASH); R_EFF(SPIKY_BALL_S3_DIE);
    R_EFF(SPIKY_THROWER_DIE); R_EFF(CRAB_DIE); R_EFF(FLY_DIE); R_EFF(EXT_TURTLE_SQUISH);
    R_EFF(EXT_TURTLE_DIE); R_EFF(YELSWITCH_FODDER_SQUISH); R_EFF(YELSWITCH_FODDER_DIE); R_EFF(BLUSWITCH_FODDER_SQUISH);
    R_EFF(BLUSWITCH_FODDER_DIE); R_EFF(GRNSWITCH_FODDER_SQUISH); R_EFF(GRNSWITCH_FODDER_DIE); R_EFF(REDSWITCH_FODDER_SQUISH);
    R_EFF(REDSWITCH_FODDER_DIE); R_EFF(BIG_FODDER_SQUISH); R_EFF(BIG_FODDER_DIE); R_EFF(BIG_SHELL_DIE);
    R_EFF(POWER_S3_DIE); R_EFF(JUMPER_S4_DIE); R_EFF(VILLAIN_S3_DIE); R_EFF(BLOCK_S1_SMASH);
    R_EFF(FODDER_S1_SQUISH); R_EFF(FODDER_S1_DIE); R_EFF(DOOR_S2_OPEN); R_EFF(DOOR_DOUBLE_S3_OPEN);
    R_EFF(ITEM_POD_OPEN); R_EFF(ITEM_POD_BREAK); R_EFF(PET_BIRTH); R_EFF(DOOR_SIDE_S3_OPEN);
    R_EFF(SHELL_S4_DIE); R_EFF(HIT_TURTLE_S4_DIE); R_EFF(HIT_TURTLE_S4_SQUISH); R_EFF(SMOKE_S5);
    R_EFF(BIRD_DIE); R_EFF(RED_SPIT_GUY_DIE); R_EFF(BLU_SPIT_GUY_DIE); R_EFF(GRY_SPIT_GUY_DIE);
    R_EFF(SPIT_GUY_BALL_DIE); R_EFF(BOMB_S2_EXPLODE); R_EFF(BOMB_S3_EXPLODE_SEED); R_EFF(BOMB_S3_EXPLODE);
    R_EFF(WALK_BOMB_S3_DIE); R_EFF(WHIP); R_EFF(SKID_DUST); R_EFF(WHACK);
    R_EFF(BOOT_STOMP); R_EFF(PLR_FIREBALL_TRAIL); R_EFF(COIN_COLLECT); R_EFF(SCORE);
    R_EFF(SPARKLE); R_EFF(COIN_SWITCH_PRESS); R_EFF(SPINBLOCK); R_EFF(CARRY_BUDDY_DIE);
    R_EFF(BRUTE_SQUISH); R_EFF(BRUTE_SQUISHED_DIE); R_EFF(BRUTE_DIE); R_EFF(BIG_GUY_DIE);
    R_EFF(CARRY_FODDER_DIE); R_EFF(CHASER_DIE); R_EFF(STONE_S3_DIE); R_EFF(BIG_GHOST_DIE);
    R_EFF(GHOST_S4_DIE); R_EFF(GHOST_FAST_DIE); R_EFF(GHOST_S3_DIE); R_EFF(GRN_SHELL_S1_DIE);
    R_EFF(RED_SHELL_S1_DIE); R_EFF(SKELETON_DIE); R_EFF(STONE_S4_DIE); R_EFF(SAW_DIE);
    R_EFF(GRY_BLOCK_SMASH); R_EFF(RED_BOOT_DIE); R_EFF(BLU_BOOT_DIE); R_EFF(BIG_DOOR_OPEN);
    R_EFF(LAVA_MONSTER_LOOK); R_EFF(VILLAIN_S1_DIE); R_EFF(SICK_BOSS_DIE); R_EFF(SPACE_BLOCK_SMASH);
    R_EFF(BOSS_FRAGILE_EXPLODE); R_EFF(WALL_TURTLE_DIE); R_EFF(WALL_SPARK_DIE); R_EFF(BOSS_CASE_BREAK);
    R_EFF(BOSS_FRAGILE_DIE); R_EFF(AIR_BUBBLE); R_EFF(WATER_SPLASH); R_EFF(GRN_FISH_S3_DIE);
    R_EFF(RED_FISH_S3_DIE); R_EFF(SQUID_S3_DIE); R_EFF(GRN_FISH_S4_DIE); R_EFF(GRN_FISH_S1_DIE);
    R_EFF(BONE_FISH_DIE); R_EFF(SQUID_S1_DIE); R_EFF(YEL_FISH_S4_DIE); R_EFF(TIME_SWITCH_PRESS);
    R_EFF(TNT_PRESS); R_EFF(EARTHQUAKE_BLOCK_HIT); R_EFF(FODDER_S5_SQUISH); R_EFF(FODDER_S5_DIE);
    R_EFF(STACKER_DIE); R_EFF(CHAR3_DIE); R_EFF(CHAR4_DIE); R_EFF(SMOKE_S4);
    R_EFF(STOMP_INIT); R_EFF(STOMP_STAR); R_EFF(CHAR5_DIE); R_EFF(DIRT_BLOCK_SMASH);
    R_EFF(FIRE_DISK_DIE); R_EFF(WALK_PLANT_DIE); R_EFF(BOMBER_BOSS_DIE); R_EFF(PLR_ICEBALL_TRAIL);
    R_EFF(MAGIC_BOSS_DIE); R_EFF(BAT_DIE); R_EFF(VINE_BUG_DIE); R_EFF(FIRE_BOSS_DIE);
    R_EFF(BUBBLE_POP); R_EFF(ITEM_THROWER_DIE); R_EFF(SPIKY_S4_DIE); R_EFF(SMOKE_S2);
    R_EFF(CHAR3_HEAVY_EXPLODE); R_EFF(GENERIC_NPC_DIE); R_EFF(GENERIC_NPC_SQUISH);

#undef R_NPC
#undef R_SFX
#undef R_EFF

    // smbx2compat: mem() reads through the FFI (see LuaMisc::ffi_mem_get)
    lua_pushlightuserdata(L, reinterpret_cast<void *>(&LuaMisc::ffi_mem_get));
    lua_setglobal(L, "__xtech_ffiMemGet");

    // smbx2compat: where LunaLua's extended fields live (SMBX2NPCExt_t etc., globals.h)
    pushExtendedFieldsLayout(L);
}
