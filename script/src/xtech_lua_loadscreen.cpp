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

// smbx2compat: SMBX2's loading screens (LunaLua's Misc/LoadScreen.cpp).
//
// LunaLua runs scripts/base/engine/loadscreen.lua, which runs the episode's
// loadscreen.lua, in a second Lua state on its own thread while SMBX loads a
// level. TheXTech loads on one thread, so the loading screen is drawn from
// inside the load instead: xtech_lua_loadScreenUpdate() is called between the
// engine's load steps and for every file the level's scripts read (boot.lua's
// core.readFile), and draws a frame once 15 ms have passed since the last one
// (LunaLua's thread sleeps 15 ms between frames).
//
// Like LunaLua's, the state lasts the session (a new episode gets a new one).
// boot.lua sets it up with __xtech_loadScreen set and runs core/loadscreen.lua,
// which runs SMBX2's file. Its drawing uses the draw module's own set of images,
// shaders and queue (xtech_lua_drawSwapLoadScreen), swapped in while it runs.

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
}

#include <Logger/logger.h>
#include <AppPath/app_path.h>
#include <Utils/files.h>

#include "globals.h"
#include "global_dirs.h"
#include "config.h"
#include "frame_timer.h"
#include "pge_delay.h"
#include "core/render.h"
#include "core/events.h"

#include "xtech_lua_main.h"
#include "xtech_lua_draw.h"
#include "xtech_lua_loadscreen.h"

namespace
{

typedef std::chrono::steady_clock Clock;

lua_State *s_L = nullptr;
std::string s_episode;      // the episode the state was made for
bool s_failed = false;      // it couldn't start: no loading screens this session
bool s_active = false;
bool s_inFrame = false;

Clock::time_point s_start, s_lastFrame;
// for the log line at the end of each loading screen
int s_frames = 0;
double s_frameMs = 0;        // time spent drawing them
double s_longestGap = 0;
const char *s_longestGapAt = "";

constexpr double c_frameGapMs = 15.0;

double msSince(Clock::time_point t)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

// Runs a function of the state's _G with nargs arguments on the stack; false
// (logged) if it raised. core/loadscreen.lua's functions catch the scripts'
// own errors, so this is for errors in ours.
bool call(const char *name, int nargs, int nresults)
{
    lua_getglobal(s_L, name);
    lua_insert(s_L, -(nargs + 1));
    if(lua_pcall(s_L, nargs, nresults, 0) != 0)
    {
        pLogWarning("Lua: loading screen %s: %s", name, lua_tostring(s_L, -1));
        lua_pop(s_L, 1);
        return false;
    }
    return true;
}

void closeState()
{
    if(!s_L)
        return;

    // its images, shaders and buffers free themselves as it closes
    xtech_lua_drawSwapLoadScreen();
    lua_close(s_L);
    xtech_lua_drawFrameEnd();
    xtech_lua_drawSwapLoadScreen();
    s_L = nullptr;
}

bool boot(const std::string &episode)
{
    std::string dataRoot, coreDir;
    if(!xtech_lua_findSmbx2Core(dataRoot, coreDir))
        return false;

    Files::Data data = Files::load_file(coreDir + "/boot.lua");
    if(data.empty())
        return false;

    s_L = xtech_lua_newState(false);
    if(!s_L)
        return false;

    lua_pushboolean(s_L, 1);
    lua_setglobal(s_L, "__xtech_loadScreen");
    lua_pushstring(s_L, dataRoot.c_str());
    lua_setglobal(s_L, "__xtech_smbx2Path");
    lua_pushstring(s_L, coreDir.c_str());
    lua_setglobal(s_L, "__xtech_coreDir");
    lua_pushboolean(s_L, 0);
    lua_setglobal(s_L, "__xtech_isOverworld");
    lua_pushstring(s_L, episode.c_str());
    lua_setglobal(s_L, "__xtech_episodePath");
    lua_pushstring(s_L, "");
    lua_setglobal(s_L, "__xtech_levelFile");
    lua_pushstring(s_L, AppPathManager::logsDir().c_str());
    lua_setglobal(s_L, "__xtech_logsDir");

    xtech_lua_drawSwapLoadScreen();
    bool ok = luaL_loadbuffer(s_L, data.c_str(), data.size(), "smbx2core/boot.lua") == 0;
    if(ok)
        ok = lua_pcall(s_L, 0, 0, 0) == 0;
    if(!ok)
    {
        pLogWarning("Lua: the loading screen couldn't start: %s", lua_tostring(s_L, -1));
        lua_pop(s_L, 1);
    }
    xtech_lua_drawSwapLoadScreen();

    if(!ok)
    {
        closeState();
        return false;
    }

    s_episode = episode;
    return true;
}

// One frame of the loading screen; returns its _loadScreenTimeout (seconds)
double drawFrame(bool finished)
{
    Clock::time_point frameStart = Clock::now();
    s_inFrame = true;
    xtech_lua_drawSwapLoadScreen();

    // it asked for SMBX2's framebuffer size; the level's config may have reset it
    int w, h;
    xtech_lua_drawLoadScreenSize(w, h);
    if(w > 0 && h > 0 && (g_config.internal_res.m_value.first != w || g_config.internal_res.m_value.second != h))
    {
        ConfigChangeSentinel sent(ConfigSetLevel::script);
        g_config.internal_res = {w, h};
    }

    XRender::setTargetTexture();
    XRender::resetViewport();
    XRender::clearBuffer();

    double timeout = 0;
    lua_pushboolean(s_L, finished);
    if(call("__xtech_loadScreenFrame", 1, 1))
    {
        timeout = lua_tonumber(s_L, -1);
        lua_pop(s_L, 1);
    }

    // LunaLua draws it as camera 0, the whole screen
    xtech_lua_drawCameraStart(1);
    xtech_lua_drawBelow(XTECH_LUA_PRI_ALL);
    xtech_lua_cameraEnd();
    XRender::repaint();
    xtech_lua_drawFrameEnd();

    xtech_lua_drawSwapLoadScreen();
    XEvents::doEvents();

    s_frames++;
    s_frameMs += msSince(frameStart);
    s_lastFrame = Clock::now();
    s_inFrame = false;
    return timeout;
}

int lua_loadScreenUpdate(lua_State *)
{
    xtech_lua_loadScreenUpdate("scripts");
    return 0;
}

} // namespace


bool xtech_lua_loadScreenStart()
{
    // still up from a level that ended before it was drawn
    if(s_active)
    {
        xtech_lua_loadScreenUpdate("level closed");
        return true;
    }
    if(s_failed || !GameIsActive || !g_config.lua_enable_engine)
        return false;

    // SMBX2COMPAT_LOADSCREEN=0: the engine's enter screen instead (to compare load times)
    const char *env = std::getenv("SMBX2COMPAT_LOADSCREEN");
    if(env && std::strcmp(env, "0") == 0)
        return false;

    std::string dataRoot, coreDir;
    if(!xtech_lua_findSmbx2Core(dataRoot, coreDir))
        return false;

    std::string episode = FileNamePath.empty() ? g_dirEpisode.getCurDir() : FileNamePath;
    if(!episode.empty() && episode.back() != '/')
        episode.push_back('/');

    // SMATRS's loading screen finds its files through SMBX's episode path
    // (mem 0x00B2C61C, FileNamePath), which a level test sets only in OpenLevel
    if(FileNamePath.empty())
        FileNamePath = episode;

    if(s_L && s_episode != episode)
        closeState();
    if(!s_L)
    {
        Clock::time_point bootStart = Clock::now();
        if(!boot(episode))
        {
            s_failed = true;
            return false;
        }
        pLogInfo("Lua: loading screen state started in %.0f ms", msSince(bootStart));
    }

    xtech_lua_drawSwapLoadScreen();
    bool ok = call("__xtech_loadScreenInit", 0, 0);
    xtech_lua_drawSwapLoadScreen();
    if(!ok)
        return false;

    s_active = true;
    s_start = Clock::now();
    s_frames = 0;
    s_frameMs = 0;
    s_longestGap = 0;
    s_longestGapAt = "";
    drawFrame(false);
    return true;
}

void xtech_lua_loadScreenUpdate(const char *where)
{
    if(!s_active || s_inFrame)
        return;

    double gap = msSince(s_lastFrame);
    if(gap < c_frameGapMs)
        return;

    if(gap > s_longestGap)
    {
        s_longestGap = gap;
        s_longestGapAt = where;
    }
    drawFrame(false);
}

void xtech_lua_loadScreenEnd()
{
    if(!s_active || s_inFrame)
        return;

    double loadMs = msSince(s_start);

    // LunaLoadScreenKill: frames go on, now with _loadingFinished set, until
    // _loadScreenTimeout seconds have passed since the loading screen started
    // (SMATRS raises it until its closing animation is done)
    while(GameIsActive)
    {
        while(msSince(s_lastFrame) < c_frameGapMs)
            PGE_Delay(1);
        double timeout = drawFrame(true);
        if(msSince(s_start) >= timeout * 1000.0)
            break;
    }

    s_active = false;
    pLogInfo("Lua: loading screen: loaded in %.0f ms, closed after %.0f ms; %d frames of %.1f ms, longest gap %.0f ms (%s)",
             loadMs, msSince(s_start), s_frames, s_frames ? s_frameMs / s_frames : 0.0, s_longestGap, s_longestGapAt);

    // the old episode script's images and shaders go now, not at some later frame
    xtech_lua_drawSwapLoadScreen();
    lua_gc(s_L, LUA_GCCOLLECT, 0);
    xtech_lua_drawFrameEnd();
    xtech_lua_drawSwapLoadScreen();

    // LunaLua's g_ResetFrameTiming
    resetFrameTimer();
}

void xtech_lua_loadScreenQuit()
{
    s_active = false;
    closeState();
}

int xtech_lua_loadScreenLuaKB()
{
    return s_L ? lua_gc(s_L, LUA_GCCOUNT, 0) : -1;
}

void xtech_lua_register_loadscreen(lua_State *L)
{
    lua_register(L, "xtech_misc_loadScreenUpdate", lua_loadScreenUpdate);
}
