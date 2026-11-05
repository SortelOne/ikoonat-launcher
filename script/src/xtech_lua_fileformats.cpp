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

// smbx2compat: the data behind LunaLua's FileFormats table (level, world and
// npc-N.txt files read into Lua tables). Field names follow LunaLua's
// LuaProxyFileFormats.cpp. Custom params are left as JSON strings; the Lua
// side (smbx2compat/core/host.lua) decodes them with SMBX2's JSON library.

#include <string>

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
}

#include <PGE_File_Formats/file_formats.h>

#include "xtech_lua_fileformats.h"

namespace
{

// Each setField stores a value into the table at the top of the stack
template<class V>
void setField(lua_State *L, const char *key, V v) // integers and enums
{
    lua_pushnumber(L, static_cast<lua_Number>((long long)v));
    lua_setfield(L, -2, key);
}

void setField(lua_State *L, const char *key, double v)
{
    lua_pushnumber(L, v);
    lua_setfield(L, -2, key);
}

void setField(lua_State *L, const char *key, float v)
{
    lua_pushnumber(L, v);
    lua_setfield(L, -2, key);
}

void setField(lua_State *L, const char *key, bool v)
{
    lua_pushboolean(L, v);
    lua_setfield(L, -2, key);
}

void setField(lua_State *L, const char *key, const std::string &v)
{
    lua_pushlstring(L, v.data(), v.size());
    lua_setfield(L, -2, key);
}

void setStringList(lua_State *L, const char *key, const PGESTRINGList &list)
{
    lua_createtable(L, (int)list.size(), 0);
    int n = 0;
    for(const PGESTRING &s : list)
    {
        lua_pushlstring(L, s.data(), s.size());
        lua_rawseti(L, -2, ++n);
    }
    lua_setfield(L, -2, key);
}

void pushFileMeta(lua_State *L, const FileFormatMeta &m)
{
    lua_newtable(L);
    setField(L, "isValid", m.ReadFileValid);
    lua_newtable(L);
    setField(L, "line", m.ERROR_linenum);
    setField(L, "lineData", m.ERROR_linedata);
    setField(L, "info", m.ERROR_info);
    lua_setfield(L, -2, "error");
    lua_newtable(L);
    setField(L, "type", m.RecentFormat);
    setField(L, "recentVersion", m.RecentFormatVersion);
    lua_setfield(L, -2, "format");
    setField(L, "fileName", m.filename);
    setField(L, "path", m.path);
}

// meta.data stays a JSON string here
void setElementMeta(lua_State *L, const ElementMeta &m)
{
    lua_newtable(L);
    setField(L, "arrayId", m.array_id);
    setField(L, "index", m.index);
    setField(L, "data", m.custom_params);
    lua_setfield(L, -2, "meta");
}

// Runs fill(element) for each item into a new array stored as t[key]
template<class List, class Fill>
void setArray(lua_State *L, const char *key, const List &list, Fill fill)
{
    lua_createtable(L, (int)list.size(), 0);
    int n = 0;
    for(const auto &e : list)
    {
        lua_newtable(L);
        fill(e);
        lua_rawseti(L, -2, ++n);
    }
    lua_setfield(L, -2, key);
}

void setMusicOverrides(lua_State *L, const char *key, const std::vector<LevelData::MusicOverrider> &list)
{
    setArray(L, key, list, [L](const LevelData::MusicOverrider &mo)
    {
        setField(L, "type", (int)mo.type);
        setField(L, "id", mo.id);
        setField(L, "fileName", mo.fileName);
    });
}

void pushLevelHead(lua_State *L, const LevelData &data)
{
    lua_newtable(L);
    pushFileMeta(L, data.meta);
    lua_setfield(L, -2, "meta");
    setField(L, "levelName", data.LevelName);
    setField(L, "stars", data.stars);
    setField(L, "openLevelOnFail", data.open_level_on_fail);
    setField(L, "openLevelOnFailWarpId", data.open_level_on_fail_warpID);
    setField(L, "data", data.custom_params);
}

void pushLevel(lua_State *L, const LevelData &data)
{
    pushLevelHead(L, data);

    setMusicOverrides(L, "musicOverrides", data.music_overrides);
    setMusicOverrides(L, "soundOverrides", data.sound_overrides);

    setArray(L, "sections", data.sections, [L](const LevelSection &s)
    {
        setField(L, "id", s.id);
        setField(L, "sizeTop", s.size_top);
        setField(L, "sizeBottom", s.size_bottom);
        setField(L, "sizeLeft", s.size_left);
        setField(L, "sizeRight", s.size_right);
        setField(L, "musicId", s.music_id);
        setField(L, "musicFile", s.music_file);
        setField(L, "bgColor", s.bgcolor);
        setField(L, "backgroundId", s.background);
        setField(L, "wrapH", s.wrap_h);
        setField(L, "wrapV", s.wrap_v);
        setField(L, "offScreenExit", s.OffScreenEn);
        setField(L, "lightingValue", s.lighting_value);
        setField(L, "lockLeftScrool", s.lock_left_scroll);
        setField(L, "lockRightScrool", s.lock_right_scroll);
        setField(L, "lockUpScrool", s.lock_up_scroll);
        setField(L, "lockDownScrool", s.lock_down_scroll);
        setField(L, "isUnderWater", s.underwater);
        setField(L, "data", s.custom_params);
    });

    setArray(L, "players", data.players, [L](const PlayerPoint &p)
    {
        setField(L, "id", p.id);
        setField(L, "x", p.x);
        setField(L, "y", p.y);
        setField(L, "w", p.w);
        setField(L, "h", p.h);
        setField(L, "direction", p.direction);
    });

    setArray(L, "blocks", data.blocks, [L](const LevelBlock &b)
    {
        setField(L, "x", b.x);
        setField(L, "y", b.y);
        setField(L, "w", b.w);
        setField(L, "h", b.h);
        setField(L, "autoscale", b.autoscale);
        setField(L, "id", b.id);
        setField(L, "npcId", b.npc_id);
        setField(L, "invisible", b.invisible);
        setField(L, "motionAiId", b.motion_ai_id);
        setField(L, "gfxName", b.gfx_name);
        setField(L, "gfx_dx", b.gfx_dx);
        setField(L, "gfx_dy", b.gfx_dy);
        setField(L, "layer", b.layer);
        setField(L, "eventDestroy", b.event_destroy);
        setField(L, "eventHit", b.event_hit);
        setField(L, "eventEmptyLayer", b.event_emptylayer);
        setField(L, "eventOnScreen", b.event_on_screen);
        setElementMeta(L, b.meta);
    });

    setArray(L, "bgo", data.bgo, [L](const LevelBGO &b)
    {
        setField(L, "x", b.x);
        setField(L, "y", b.y);
        setField(L, "id", b.id);
        setField(L, "zMode", b.z_mode);
        setField(L, "zOffset", b.z_offset);
        setField(L, "gfx_dx", b.gfx_dx);
        setField(L, "gfx_dy", b.gfx_dy);
        setField(L, "layer", b.layer);
        setElementMeta(L, b.meta);
    });

    setArray(L, "npc", data.npc, [L](const LevelNPC &n)
    {
        setField(L, "x", n.x);
        setField(L, "y", n.y);
        setField(L, "direction", n.direct);
        setField(L, "id", n.id);
        setField(L, "gfxName", n.gfx_name);
        setField(L, "gfx_dx", n.gfx_dx);
        setField(L, "gfx_dy", n.gfx_dy);
        setField(L, "contents", n.contents);
        setField(L, "specialData", n.special_data);
        setField(L, "specialData2", n.special_data2);
        lua_newtable(L);
        setField(L, "enabled", n.generator);
        setField(L, "direction", n.generator_direct);
        setField(L, "type", n.generator_type);
        setField(L, "period", n.generator_period);
        setField(L, "periodFrames", n.generator_period_orig);
        setField(L, "periodOrignUnit", (int)n.generator_period_orig_unit);
        setField(L, "customAngle", n.generator_custom_angle);
        setField(L, "initialSpeed", n.generator_initial_speed);
        lua_setfield(L, -2, "generator");
        setField(L, "msg", n.msg);
        setField(L, "friendly", n.friendly);
        setField(L, "noMove", n.nomove);
        setField(L, "isBoss", n.is_boss);
        setField(L, "layer", n.layer);
        setField(L, "attachLayer", n.attach_layer);
        lua_newtable(L);
        setField(L, "activate", n.event_activate);
        setField(L, "die", n.event_die);
        setField(L, "talk", n.event_talk);
        setField(L, "emptyLayer", n.event_emptylayer);
        setField(L, "grab", n.event_grab);
        setField(L, "nextFrame", n.event_nextframe);
        setField(L, "touch", n.event_touch);
        lua_setfield(L, -2, "event");
        setField(L, "sendIdToVariable", n.send_id_to_variable);
        setElementMeta(L, n.meta);
    });

    setArray(L, "warps", data.doors, [L](const LevelDoor &w)
    {
        setField(L, "ix", w.ix);
        setField(L, "iy", w.iy);
        setField(L, "isSetIn", w.isSetIn);
        setField(L, "ox", w.ox);
        setField(L, "oy", w.oy);
        setField(L, "isSetOut", w.isSetOut);
        setField(L, "twoWay", w.two_way);
        setField(L, "iDirect", w.idirect);
        setField(L, "oDirect", w.odirect);
        setField(L, "type", w.type);
        setField(L, "transitionEffect", w.transition_effect);
        setField(L, "dstLevel", w.lname);
        setField(L, "dstLevelWarpId", w.warpto);
        setField(L, "levelEntrance", w.lvl_i);
        setField(L, "levelExit", w.lvl_o);
        setField(L, "dstWorldX", w.world_x);
        setField(L, "dstWorldY", w.world_y);
        setField(L, "unknown", w.unknown);
        setField(L, "isWarpInvisible", w.unknown);
        setField(L, "noVehicles", w.novehicles);
        setField(L, "allowNPC", w.allownpc);
        setField(L, "locked", w.locked);
        setField(L, "needABomb", w.need_a_bomb);
        setField(L, "hideEnteringScene", w.hide_entering_scene);
        setField(L, "allowNPCInterLevel", w.allownpc_interlevel);
        setField(L, "specialStateRequired", w.special_state_required);
        setField(L, "iLenght", w.length_i); // sic, as LunaLua spells it
        setField(L, "oLength", w.length_o);
        lua_newtable(L);
        setField(L, "enable", w.cannon_exit);
        setField(L, "speed", w.cannon_exit_speed);
        lua_setfield(L, -2, "cannonExit");
        setField(L, "layer", w.layer);
        lua_newtable(L);
        setField(L, "enter", w.event_enter);
        lua_setfield(L, -2, "event");
        setElementMeta(L, w.meta);
    });

    setArray(L, "physEnvZones", data.physez, [L](const LevelPhysEnv &p)
    {
        setField(L, "x", p.x);
        setField(L, "y", p.y);
        setField(L, "w", p.w);
        setField(L, "h", p.h);
        setField(L, "envType", p.env_type);
        setField(L, "layer", p.layer);
        lua_newtable(L);
        setField(L, "touch", p.touch_event);
        lua_setfield(L, -2, "event");
        setElementMeta(L, p.meta);
    });

    setArray(L, "layers", data.layers, [L](const LevelLayer &l)
    {
        setField(L, "name", l.name);
        setField(L, "hidden", l.hidden);
        setField(L, "locked", l.locked);
        setElementMeta(L, l.meta);
    });

    setArray(L, "events", data.events, [L](const LevelSMBX64Event &e)
    {
        setField(L, "name", e.name);
        setField(L, "msg", e.msg);
        setField(L, "soundId", e.sound_id);
        setField(L, "endGameType", e.end_game);
        setField(L, "noSmoke", e.nosmoke);
        setStringList(L, "layersToHide", e.layers_hide);
        setStringList(L, "layersToShow", e.layers_show);
        setStringList(L, "layersToToggle", e.layers_toggle);
        setArray(L, "sectionSets", e.sets, [L](const LevelEvent_Sets &s)
        {
            setField(L, "id", s.id);
            setField(L, "musicId", s.music_id);
            setField(L, "musicFile", s.music_file);
            setField(L, "backgroundId", s.background_id);
            setField(L, "positionLeft", s.position_left);
            setField(L, "positionTop", s.position_top);
            setField(L, "positionBottom", s.position_bottom);
            setField(L, "positionRight", s.position_right);
            setField(L, "expressionPosX", s.expression_pos_x);
            setField(L, "expressionPosY", s.expression_pos_y);
            setField(L, "expressionPosW", s.expression_pos_w);
            setField(L, "expressionPosH", s.expression_pos_h);
            setField(L, "autoscroll", s.autoscrol);
            setField(L, "autoscrollSpeedX", s.autoscrol_x);
            setField(L, "autoscrollSpeedY", s.autoscrol_y);
            setField(L, "expressionAutoscroolSpeedX", s.expression_autoscrool_x);
            setField(L, "expressionAutoscroolSpeedY", s.expression_autoscrool_y);
        });
        setField(L, "triggerEvent", e.trigger);
        setField(L, "triggerEventTimer", e.trigger_timer);
        setField(L, "triggerEventTimerOrigUnit", (int)e.trigger_timer_unit);
        setField(L, "triggerEventTimerOrig", e.trigger_timer_orig);
        setField(L, "controlsEnable", e.ctrls_enable);
        setField(L, "controlUp", e.ctrl_up);
        setField(L, "controlDown", e.ctrl_down);
        setField(L, "controlLeft", e.ctrl_left);
        setField(L, "controlRight", e.ctrl_right);
        setField(L, "controlJump", e.ctrl_jump);
        setField(L, "controlAltJump", e.ctrl_altjump);
        setField(L, "controlRun", e.ctrl_run);
        setField(L, "controlAltRun", e.ctrl_altrun);
        setField(L, "controlStart", e.ctrl_start);
        setField(L, "controlDrop", e.ctrl_drop);
        setField(L, "controlLockKeyboard", e.ctrl_lock_keyboard);
        setField(L, "autostart", e.autostart);
        setField(L, "autostartCondition", e.autostart_condition);
        setField(L, "triggerScript", e.trigger_script);
        setField(L, "triggerApiId", e.trigger_api_id);
        setField(L, "moveLayer", e.movelayer);
        setField(L, "moveLayerSpeedX", e.layer_speed_x);
        setField(L, "moveLayerSpeedY", e.layer_speed_y);
        setField(L, "moveCameraX", e.move_camera_x);
        setField(L, "moveCameraY", e.move_camera_y);
        setField(L, "moveCameraSection", e.scroll_section);
        setElementMeta(L, e.meta);
    });
}

void pushWorldHead(lua_State *L, WorldData &data)
{
    lua_newtable(L);
    pushFileMeta(L, data.meta);
    lua_setfield(L, -2, "meta");
    setField(L, "episodeTitle", data.EpisodeTitle);
    lua_newtable(L);
    int n = 0;
    for(bool noCharacter : data.nocharacter)
    {
        lua_pushboolean(L, noCharacter);
        lua_rawseti(L, -2, ++n);
    }
    lua_setfield(L, -2, "disableCharacters");
    setField(L, "introLevel", data.IntroLevel_file);
    setField(L, "gameOverLevel", data.GameOverLevel_file);
    setField(L, "isHubStyleWorld", data.HubStyledWorld);
    setField(L, "autoRestartLevel", data.restartlevel);
    setField(L, "restrictSinglePlayer", data.restrictSinglePlayer);
    setField(L, "restrictSecureGameSave", data.restrictSecureGameSave);
    setField(L, "disableEnterScreen", data.disableEnterScreen);
    setField(L, "cheatsPolicy", data.cheatsPolicy);
    setStringList(L, "cheatsList", data.cheatsList);
    setField(L, "saveResumePolicy", data.saveResumePolicy);
    setField(L, "saveAuto", data.saveAuto);
    setField(L, "saveLocker", data.saveLocker);
    setField(L, "saveLockerEx", data.saveLockerEx);
    setField(L, "saveLockerMsg", data.saveLockerMsg);
    setField(L, "showEverything", data.showEverything);
    setField(L, "stars", data.stars);
    setField(L, "inventoryLimit", data.inventoryLimit);
    setField(L, "authors", data.authors);
    setField(L, "data", data.custom_params);
}

void pushWorld(lua_State *L, WorldData &data)
{
    pushWorldHead(L, data);

    setArray(L, "terrain", data.tiles, [L](const WorldTerrainTile &t)
    {
        setField(L, "x", t.x);
        setField(L, "y", t.y);
        setField(L, "id", t.id);
        setField(L, "gfx_dx", t.gfx_dx);
        setField(L, "gfx_dy", t.gfx_dy);
        setField(L, "layer", t.layer);
        setElementMeta(L, t.meta);
    });

    setArray(L, "scenery", data.scenery, [L](const WorldScenery &s)
    {
        setField(L, "x", s.x);
        setField(L, "y", s.y);
        setField(L, "id", s.id);
        setField(L, "gfx_dx", s.gfx_dx);
        setField(L, "gfx_dy", s.gfx_dy);
        setField(L, "layer", s.layer);
        setElementMeta(L, s.meta);
    });

    setArray(L, "paths", data.paths, [L](const WorldPathTile &p)
    {
        setField(L, "x", p.x);
        setField(L, "y", p.y);
        setField(L, "id", p.id);
        setField(L, "gfx_dx", p.gfx_dx);
        setField(L, "gfx_dy", p.gfx_dy);
        setField(L, "layer", p.layer);
        setElementMeta(L, p.meta);
    });

    setArray(L, "levels", data.levels, [L](const WorldLevelTile &l)
    {
        setField(L, "x", l.x);
        setField(L, "y", l.y);
        setField(L, "id", l.id);
        setField(L, "gfx_dx", l.gfx_dx);
        setField(L, "gfx_dy", l.gfx_dy);
        setField(L, "levelFile", l.lvlfile);
        setField(L, "title", l.title);
        setField(L, "exitTop", l.top_exit);
        setField(L, "exitLeft", l.left_exit);
        setField(L, "exitRight", l.right_exit);
        setField(L, "exitBottom", l.bottom_exit);
        setField(L, "isAlwaysVisible", l.alwaysVisible);
        setField(L, "pathBg", l.pathbg);
        setField(L, "pathBigBg", l.bigpathbg);
        setField(L, "isGameStartPoint", l.gamestart);
        setField(L, "isForceStart", l.forceStart);
        setField(L, "disableStartCoinsCount", l.disableStarCoinsCount);
        setField(L, "destroyOnCompleting", l.destroyOnCompleting);
        setField(L, "goToX", l.gotox);
        setField(L, "goToY", l.gotoy);
        setField(L, "layer", l.layer);
        setElementMeta(L, l.meta);
    });

    setArray(L, "musicBoxes", data.music, [L](const WorldMusicBox &m)
    {
        setField(L, "x", m.x);
        setField(L, "y", m.y);
        setField(L, "id", m.id);
        setField(L, "musicFile", m.music_file);
        setField(L, "layer", m.layer);
        setElementMeta(L, m.meta);
    });
}

// xtech_fileformats_openLevel(path, headerOnly)
int openLevel(lua_State *L)
{
    std::string path = luaL_checkstring(L, 1);
    bool headerOnly = lua_toboolean(L, 2) != 0;
    LevelData data;
    if(headerOnly)
    {
        FileFormats::OpenLevelFileHeader(path, data);
        pushLevelHead(L, data);
    }
    else
    {
        FileFormats::OpenLevelFile(path, data);
        pushLevel(L, data);
    }
    return 1;
}

// xtech_fileformats_openWorld(path, headerOnly)
int openWorld(lua_State *L)
{
    std::string path = luaL_checkstring(L, 1);
    bool headerOnly = lua_toboolean(L, 2) != 0;
    WorldData data;
    if(headerOnly)
    {
        FileFormats::OpenWorldFileHeader(path, data);
        pushWorldHead(L, data);
    }
    else
    {
        FileFormats::OpenWorldFile(path, data);
        pushWorld(L, data);
    }
    return 1;
}

// xtech_fileformats_openNpcConfig(path): only the keys the file sets
int openNpcConfig(lua_State *L)
{
    std::string path = luaL_checkstring(L, 1);
    NPCConfigFile data;
    FileFormats::ReadNpcTXTFileF(path, data);

    lua_newtable(L);
    setField(L, "isValid", data.ReadFileValid);
    setField(L, "errorString", data.errorString);
    setField(L, "unknownLines", data.unknownLines);
#define NPC_KEY(name) if(data.en_##name) setField(L, #name, data.name)
    NPC_KEY(gfxoffsetx);
    NPC_KEY(gfxoffsety);
    NPC_KEY(width);
    NPC_KEY(height);
    NPC_KEY(gfxwidth);
    NPC_KEY(gfxheight);
    NPC_KEY(score);
    NPC_KEY(playerblock);
    NPC_KEY(playerblocktop);
    NPC_KEY(npcblock);
    NPC_KEY(npcblocktop);
    NPC_KEY(grabside);
    NPC_KEY(grabtop);
    NPC_KEY(jumphurt);
    NPC_KEY(nohurt);
    NPC_KEY(noblockcollision);
    NPC_KEY(cliffturn);
    NPC_KEY(noyoshi);
    NPC_KEY(foreground);
    NPC_KEY(speed);
    NPC_KEY(nofireball);
    NPC_KEY(nogravity);
    NPC_KEY(frames);
    NPC_KEY(framespeed);
    NPC_KEY(framestyle);
    NPC_KEY(noiceball);
    NPC_KEY(nohammer);
    NPC_KEY(noshell);
    NPC_KEY(name);
    NPC_KEY(description);
    NPC_KEY(health);
    NPC_KEY(image);
    NPC_KEY(icon);
    NPC_KEY(script);
    NPC_KEY(group);
    NPC_KEY(category);
    NPC_KEY(grid);
    NPC_KEY(gridoffsetx);
    NPC_KEY(gridoffsety);
    NPC_KEY(gridalign);
#undef NPC_KEY
    return 1;
}

// The enums LunaLua binds as FileFormats.<Class>.<NAME>
void pushEnums(lua_State *L)
{
    lua_newtable(L);
#define ENUM_CLASS(name) lua_newtable(L)
#define ENUM_VALUE(cls, value) setField(L, #value, cls::value)
#define ENUM_END(name) lua_setfield(L, -2, name)
    ENUM_CLASS("LevelData");
    ENUM_VALUE(LevelData, PGEX);
    ENUM_VALUE(LevelData, SMBX64);
    ENUM_VALUE(LevelData, SMBX38A);
    ENUM_END("LevelData");

    ENUM_CLASS("WorldData");
    ENUM_VALUE(WorldData, PGEX);
    ENUM_VALUE(WorldData, SMBX64);
    ENUM_VALUE(WorldData, SMBX38A);
    ENUM_END("WorldData");

    ENUM_CLASS("LevelSection");
    ENUM_VALUE(LevelSection, LIGHTING_NOT_SET);
    ENUM_VALUE(LevelSection, LIGHTING_DISABLED);
    ENUM_VALUE(LevelSection, LIGHTING_PIXELS_BEGIN);
    ENUM_END("LevelSection");

    ENUM_CLASS("LevelBGO");
    ENUM_VALUE(LevelBGO, Background2);
    ENUM_VALUE(LevelBGO, Background1);
    ENUM_VALUE(LevelBGO, ZDefault);
    ENUM_VALUE(LevelBGO, Foreground1);
    ENUM_VALUE(LevelBGO, Foreground2);
    ENUM_END("LevelBGO");

    ENUM_CLASS("LevelNPC");
    ENUM_VALUE(LevelNPC, NPC_GEN_CENTER);
    ENUM_VALUE(LevelNPC, NPC_GEN_UP);
    ENUM_VALUE(LevelNPC, NPC_GEN_LEFT);
    ENUM_VALUE(LevelNPC, NPC_GEN_DOWN);
    ENUM_VALUE(LevelNPC, NPC_GEN_RIGHT);
    ENUM_VALUE(LevelNPC, NPC_GEN_UP_LEFT);
    ENUM_VALUE(LevelNPC, NPC_GEN_LEFT_DOWN);
    ENUM_VALUE(LevelNPC, NPC_GEN_DOWN_RIGHT);
    ENUM_VALUE(LevelNPC, NPC_GEN_RIGHT_UP);
    ENUM_VALUE(LevelNPC, NPC_GENERATOR_APPEAR);
    ENUM_VALUE(LevelNPC, NPC_GENERATOR_WARP);
    ENUM_VALUE(LevelNPC, NPC_GENERATPR_PROJECTILE);
    ENUM_VALUE(LevelNPC, NPC_GENERATPR_CUSTOM1);
    ENUM_VALUE(LevelNPC, NPC_GENERATPR_CUSTOM2);
    ENUM_END("LevelNPC");

    ENUM_CLASS("LevelDoor");
    ENUM_VALUE(LevelDoor, ENTRANCE_UP);
    ENUM_VALUE(LevelDoor, ENTRANCE_LEFT);
    ENUM_VALUE(LevelDoor, ENTRANCE_DOWN);
    ENUM_VALUE(LevelDoor, ENTRANCE_RIGHT);
    ENUM_VALUE(LevelDoor, EXIT_UP);
    ENUM_VALUE(LevelDoor, EXIT_LEFT);
    ENUM_VALUE(LevelDoor, EXIT_DOWN);
    ENUM_VALUE(LevelDoor, EXIT_RIGHT);
    ENUM_VALUE(LevelDoor, WARP_INSTANT);
    ENUM_VALUE(LevelDoor, WARP_PIPE);
    ENUM_VALUE(LevelDoor, WARP_DOOR);
    ENUM_VALUE(LevelDoor, WARP_PORTAL);
    ENUM_VALUE(LevelDoor, TRANSIT_NONE);
    ENUM_VALUE(LevelDoor, TRANSIT_SCROLL);
    ENUM_VALUE(LevelDoor, TRANSIT_FADE);
    ENUM_VALUE(LevelDoor, TRANSIT_CIRCLE_FADE);
    ENUM_VALUE(LevelDoor, TRANSIT_FLIP_H);
    ENUM_VALUE(LevelDoor, TRANSIT_FLIP_V);
    ENUM_END("LevelDoor");

    ENUM_CLASS("LevelPhysEnv");
    ENUM_VALUE(LevelPhysEnv, ENV_WATER);
    ENUM_VALUE(LevelPhysEnv, ENV_QUICKSAND);
    ENUM_VALUE(LevelPhysEnv, ENV_CUSTOM_LIQUID);
    ENUM_VALUE(LevelPhysEnv, ENV_GRAVITATIONAL_FIELD);
    ENUM_VALUE(LevelPhysEnv, ENV_TOUCH_EVENT_ONCE_PLAYER);
    ENUM_VALUE(LevelPhysEnv, ENV_TOUCH_EVENT_PLAYER);
    // LunaLua's PGE names 6/7 "..._NPC" and 13/14 "..._NPC1"; ours names them "..._ANY" and "..._NPC"
    setField(L, "ENV_TOUCH_EVENT_ONCE_NPC", LevelPhysEnv::ENV_TOUCH_EVENT_ONCE_ANY);
    setField(L, "ENV_TOUCH_EVENT_NPC", LevelPhysEnv::ENV_TOUCH_EVENT_ANY);
    ENUM_VALUE(LevelPhysEnv, ENV_CLICK_EVENT);
    ENUM_VALUE(LevelPhysEnv, ENV_COLLISION_SCRIPT);
    ENUM_VALUE(LevelPhysEnv, ENV_CLICK_SCRIPT);
    ENUM_VALUE(LevelPhysEnv, ENV_COLLISION_EVENT);
    ENUM_VALUE(LevelPhysEnv, ENV_AIR);
    setField(L, "ENV_TOUCH_EVENT_ONCE_NPC1", LevelPhysEnv::ENV_TOUCH_EVENT_ONCE_NPC);
    setField(L, "ENV_TOUCH_EVENT_NPC1", LevelPhysEnv::ENV_TOUCH_EVENT_NPC);
    ENUM_VALUE(LevelPhysEnv, ENV_NPC_HURTING_FIELD);
    ENUM_VALUE(LevelPhysEnv, ENV_SUBAREA);
    ENUM_END("LevelPhysEnv");

    ENUM_CLASS("LevelEvent_Sets");
    ENUM_VALUE(LevelEvent_Sets, LESet_Nothing);
    ENUM_VALUE(LevelEvent_Sets, LESet_ResetDefault);
    ENUM_END("LevelEvent_Sets");
#undef ENUM_CLASS
#undef ENUM_VALUE
#undef ENUM_END
}

} // namespace

void xtech_lua_register_fileformats(lua_State *L)
{
    lua_register(L, "xtech_fileformats_openLevel", &openLevel);
    lua_register(L, "xtech_fileformats_openWorld", &openWorld);
    lua_register(L, "xtech_fileformats_openNpcConfig", &openNpcConfig);
    pushEnums(L);
    lua_setglobal(L, "xtech_fileformats_enums");
}
