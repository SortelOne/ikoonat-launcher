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

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <ctime>
#include <chrono>
#include <vector>

#if defined(__APPLE__)
#   include <SDL2/SDL_filesystem.h>
#   include <SDL2/SDL_stdinc.h>
#endif

#include <AppPath/app_path.h>
#include <DirManager/dirman.h>
#include <Utils/files.h>
#include <Logger/logger.h>

#include "main/smbx2_mode.h"
#include "main/smbx2_ids.h"
#include "main/episode_library.h"
#include "main/asset_pack.h"
#include "load_gfx.h"

EpisodeMode g_episodeModeOverride = EpisodeMode::Auto;
std::string g_smbx2DataOverride;

// SMBX2 mode has two parts: SMBX2's scripts (core mode) and SMBX2's object
// IDs and default sprites. The menu turns the scripts off but keeps the
// sprites: switching them takes ~1-3 s (every file is read again), so it
// happens only when an episode in the other mode starts.
static bool s_coreSmbx2 = false;
static bool s_idsSmbx2 = false;
static bool s_dataChecked = false;
static std::string s_dataDir;

const char *EpisodeMode_name(EpisodeMode mode)
{
    switch(mode)
    {
    case EpisodeMode::Smbx:
        return "smbx";
    case EpisodeMode::Smbx2:
        return "smbx2";
    default:
        return "auto";
    }
}

bool EpisodeMode_parse(const std::string &s, EpisodeMode &out)
{
    std::string l;
    for(char c : s)
        l.push_back((char)std::tolower((unsigned char)c));

    if(l == "auto")
        out = EpisodeMode::Auto;
    else if(l == "smbx" || l == "smbx13" || l == "thextech")
        out = EpisodeMode::Smbx;
    else if(l == "smbx2")
        out = EpisodeMode::Smbx2;
    else
        return false;

    return true;
}

static std::string s_withSlash(std::string dir)
{
    if(!dir.empty() && dir.back() != '/')
        dir.push_back('/');
    return dir;
}

static bool s_isSmbx2Data(const std::string &dir)
{
    return !dir.empty() && Files::fileExists(s_withSlash(dir) + "scripts/base/engine/main.lua");
}

std::string SMBX2Data_dir()
{
    if(s_dataChecked)
        return s_dataDir;

    s_dataChecked = true;
    s_dataDir.clear();

    const std::string candidates[] =
    {
        g_smbx2DataOverride,
        Library_smbx2DataDir(),
        s_withSlash(AppPathManager::userAppDirSTD()) + "smbx2",
        AppPathManager::assetsRoot() + "smbx2",
    };

    for(const std::string &c : candidates)
    {
        if(s_isSmbx2Data(c))
        {
            s_dataDir = s_withSlash(c);
            break;
        }
    }

    if(!g_smbx2DataOverride.empty() && s_dataDir != s_withSlash(g_smbx2DataOverride))
        pLogWarning("smbx2compat: --smbx2-data %s has no SMBX2 scripts", g_smbx2DataOverride.c_str());

    return s_dataDir;
}

std::string SMBX2Core_dir()
{
    std::string dev = AppPathManager::assetsRoot() + "scripts/smbx2core/";
    if(Files::fileExists(dev + "boot.lua"))
        return dev;

#if defined(__APPLE__)
    // the copy in the app bundle (Contents/Resources/smbx2core, CMakeLists.txt)
    char *base = SDL_GetBasePath();
    if(base)
    {
        std::string bundled = s_withSlash(base) + "smbx2core/";
        SDL_free(base);
        if(Files::fileExists(bundled + "boot.lua"))
            return bundled;
    }
#endif

    // where a port copies it out (Android: from the APK)
    std::string user = s_withSlash(AppPathManager::userAppDirSTD()) + "smbx2core/";
    if(Files::fileExists(user + "boot.lua"))
        return user;

    return std::string();
}


// ---- auto-detect ----

static std::string s_lower(const std::string &s)
{
    std::string l;
    l.reserve(s.size());
    for(char c : s)
        l.push_back((char)std::tolower((unsigned char)c));
    return l;
}

static bool s_endsWith(const std::string &s, const char *suffix)
{
    size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// "npc-400.png" -> true: a custom sprite or config for an ID SMBX 1.3 and
// TheXTech don't have (TheXTech's own: NPCs 301-312, blocks up to 730)
static bool s_idPastClassic(const std::string &lowerName)
{
    struct Range { const char *prefix; int max; };
    static const Range ranges[] =
    {
        {"background2-", 100},
        {"background-", 200},
        {"block-", 730},
        {"npc-", 312},
        {"effect-", 200},
    };

    for(const Range &r : ranges)
    {
        size_t n = std::strlen(r.prefix);
        if(lowerName.compare(0, n, r.prefix) != 0)
            continue;

        size_t i = n;
        int id = 0;
        while(i < lowerName.size() && std::isdigit((unsigned char)lowerName[i]) && id < 100000)
            id = id * 10 + (lowerName[i++] - '0');

        // only "<prefix><id>.<ext>" counts (not "npc-400e.png" or "npc-1-2.png")
        if(i == n || i >= lowerName.size() || lowerName[i] != '.')
            return false;

        return id > r.max;
    }

    return false;
}

// LVLX/WLDX keep the config pack they were made with in their HEAD (CPID)
static bool s_savedForSmbx2(const std::string &path)
{
    FILE *f = Files::utf8_fopen(path.c_str(), "rb");
    if(!f)
        return false;

    char buf[4096];
    size_t got = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    buf[got] = '\0';

    const char *head_end = std::strstr(buf, "HEAD_END");
    const char *cpid = std::strstr(buf, "CPID:\"");
    if(!cpid || (head_end && cpid > head_end))
        return false;

    cpid += 6;
    return (std::tolower((unsigned char)cpid[0]) == 's' && std::tolower((unsigned char)cpid[1]) == 'm'
         && std::tolower((unsigned char)cpid[2]) == 'b' && std::tolower((unsigned char)cpid[3]) == 'x'
         && cpid[4] == '2');
}

struct DetectWalk
{
    int budget = 20000; // entries looked at, so a level in a huge folder stays quick
    std::string why;
};

static bool s_walk(const std::string &dir, int depth, DetectWalk &w)
{
    DirMan d(dir);
    std::vector<std::string> files;
    if(!d.getListOfFiles(files))
        return false;

    for(const std::string &name : files)
    {
        if(--w.budget < 0)
            return false;

        std::string l = s_lower(name);

        if(s_endsWith(l, ".lua"))
        {
            w.why = "Lua scripts (" + name + ")";
            return true;
        }

        if((s_endsWith(l, ".lvlx") || s_endsWith(l, ".wldx")) && s_savedForSmbx2(dir + name))
        {
            w.why = "saved by SMBX2's editor (" + name + ")";
            return true;
        }

        if(s_idPastClassic(l))
        {
            w.why = "SMBX2 objects (" + name + ")";
            return true;
        }
    }

    if(depth >= 2)
        return false;

    std::vector<std::string> folders;
    if(!d.getListOfFolders(folders))
        return false;

    for(const std::string &name : folders)
    {
        if(name.empty() || name[0] == '.')
            continue;
        if(s_walk(dir + name + "/", depth + 1, w))
            return true;
        if(w.budget < 0)
            return false;
    }

    return false;
}

EpisodeMode EpisodeMode_detect(const std::string &episodeDir, std::string &why)
{
    DetectWalk w;
    if(s_walk(s_withSlash(episodeDir), 0, w))
    {
        why = w.why;
        return EpisodeMode::Smbx2;
    }

    why = (w.budget < 0) ? "nothing SMBX2-only in the first 20000 files" : "nothing SMBX2-only";
    return EpisodeMode::Smbx;
}


// ---- switching ----

static bool s_smbx2Allowed()
{
    const char *mode = std::getenv("SMBX2COMPAT_MODE");
    if(mode && std::strcmp(mode, "shim") == 0)
        return false;

    return !SMBX2Data_dir().empty();
}

static void s_apply(bool core, bool ids)
{
    s_coreSmbx2 = core;
    s_idsSmbx2 = ids;

    bool was = g_smbx2Ids;
    SMBX2Ids_Detect();

    // the default sprites were registered for the other mode
    if(was != g_smbx2Ids && g_AssetsLoaded)
    {
        auto t0 = std::chrono::steady_clock::now();
        ReloadDefaultObjectGFX();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        pLogInfo("smbx2compat: switched default sprites to %s's in %d ms", g_smbx2Ids ? "SMBX2" : "the asset pack", (int)ms);
    }
}

void EpisodeMode_begin(const std::string &episodeDir, int slot)
{
    s_dataChecked = false; // the importer may have added SMBX2's data

    std::string key = Library_key(episodeDir);
    LibraryEpisode_t e;
    Library_get(key, e);

    EpisodeMode chosen = EpisodeMode::Auto;
    const char *source = "auto";

    if(g_episodeModeOverride != EpisodeMode::Auto)
    {
        chosen = g_episodeModeOverride;
        source = "--episode-mode";
    }
    else if(EpisodeMode_parse(e.mode, chosen) && chosen != EpisodeMode::Auto)
        source = "library setting";

    std::string why;
    EpisodeMode detected = EpisodeMode_detect(episodeDir, why);
    e.detected = EpisodeMode_name(detected);
    e.detectedWhy = why;

    bool fromAuto = (chosen == EpisodeMode::Auto);
    if(fromAuto)
        chosen = detected;

    bool smbx2 = (chosen == EpisodeMode::Smbx2);

    if(smbx2 && SMBX2Data_dir().empty())
        pLogWarning("smbx2compat: %s wants SMBX2 mode, but SMBX2's data isn't installed; playing in SMBX mode", key.c_str());

    pLogInfo("smbx2compat: episode %s: %s mode (%s%s%s)", key.c_str(), smbx2 ? "SMBX2" : "SMBX",
             source, fromAuto ? ": " : "; detected: ", why.c_str());

    e.lastPlayed = (int64_t)std::time(nullptr);
    if(slot > 0)
        e.lastSlot = slot;
    Library_set(key, e);

    // SMATRS hands each loading screen its card in <episode>/_loadscreen/data.txt,
    // written as a level ends (onExit) and emptied as it is read. A game closed
    // inside a level (Android: swiped away) or left from Ikoonat's menu there
    // left that level's card behind, and the next start showed it while loading
    // the map. Emptied here, the start gets the plain loading screen, as after
    // SMATRS's own quit.
    if(smbx2 && slot > 0)
    {
        std::string cardFile = episodeDir;
        if(!cardFile.empty() && cardFile.back() != '/')
            cardFile.push_back('/');
        cardFile += "_loadscreen/data.txt";
        if(Files::fileExists(cardFile))
        {
            FILE *f = Files::utf8_fopen(cardFile.c_str(), "wb");
            if(f)
                std::fclose(f);
        }
    }

    s_apply(smbx2, smbx2);
}

void EpisodeMode_beginMenu()
{
    if(s_coreSmbx2)
        pLogInfo("smbx2compat: main menu: SMBX2 scripts off (keeping %s sprites)", s_idsSmbx2 ? "SMBX2's" : "the asset pack's");
    s_apply(false, s_idsSmbx2);
}

void EpisodeMode_beginStartup()
{
    // the sprites of the episode played last, so playing it again swaps nothing
    LibraryEpisode_t e;
    std::string key;
    if(!Library_mostRecent(key, e))
        return;

    EpisodeMode m = EpisodeMode::Auto;
    if(g_episodeModeOverride != EpisodeMode::Auto)
        m = g_episodeModeOverride;
    else if(!EpisodeMode_parse(e.mode, m) || m == EpisodeMode::Auto)
        EpisodeMode_parse(e.detected, m);

    s_coreSmbx2 = false;
    s_idsSmbx2 = (m == EpisodeMode::Smbx2);
    pLogInfo("smbx2compat: starting with %s sprites (last played: %s)", s_idsSmbx2 ? "SMBX2's" : "the asset pack's", key.c_str());
}

bool EpisodeMode_isSmbx2()
{
    return s_coreSmbx2 && s_smbx2Allowed();
}

bool EpisodeMode_idsSmbx2()
{
    return s_idsSmbx2 && s_smbx2Allowed();
}
