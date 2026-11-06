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

#include <IniProcessor/ini_processing.h>
#include <AppPath/app_path.h>

#include "main/episode_library.h"

#include <vector>

static std::string s_libraryFile()
{
    return AppPathManager::settingsRoot() + "library.ini";
}

// "/a/b/../c/./d" -> "/a/c/d/": the same folder named two ways gets one key
static std::string s_normalize(const std::string &path)
{
    std::vector<std::string> parts;
    size_t i = 0;
    while(i <= path.size())
    {
        size_t j = path.find('/', i);
        if(j == std::string::npos)
            j = path.size();

        std::string part = path.substr(i, j - i);
        if(part == "..")
        {
            if(!parts.empty() && parts.back() != "..")
                parts.pop_back();
            else
                parts.push_back(part);
        }
        else if(!part.empty() && part != ".")
            parts.push_back(part);

        i = j + 1;
    }

    std::string out = (!path.empty() && path[0] == '/') ? "/" : "";
    for(const std::string &part : parts)
        out += part + "/";

    return out;
}

std::string Library_key(const std::string &episodeDir)
{
    std::string dir = s_normalize(episodeDir);
    std::string user = s_normalize(AppPathManager::userAppDirSTD());

    if(!user.empty() && dir.size() > user.size() && dir.compare(0, user.size(), user) == 0)
        dir = dir.substr(user.size());

    // INI group names end at ']'
    for(char &c : dir)
    {
        if(c == '[' || c == ']')
            c = '_';
    }

    if(!dir.empty() && dir.back() == '/')
        dir.pop_back();

    return dir;
}

bool Library_get(const std::string &key, LibraryEpisode_t &out)
{
    out = LibraryEpisode_t();

    IniProcessing ini;
    if(!ini.open(s_libraryFile()))
        return false;

    if(!ini.beginGroup(key))
        return false;

    ini.read("mode", out.mode, "auto");
    ini.read("detected", out.detected, "");
    ini.read("detected-why", out.detectedWhy, "");
    long long lastPlayed = 0;
    ini.read("last-played", lastPlayed, 0LL);
    out.lastPlayed = (int64_t)lastPlayed;
    ini.read("last-slot", out.lastSlot, 0);
    ini.read("character", out.character, 1);
    ini.read("players", out.players, 1);
    ini.endGroup();

    return true;
}

void Library_set(const std::string &key, const LibraryEpisode_t &e)
{
    IniProcessing ini;
    ini.open(s_libraryFile());

    ini.beginGroup(key);
    ini.setValue("mode", e.mode);
    ini.setValue("detected", e.detected);
    ini.setValue("detected-why", e.detectedWhy);
    ini.setValue("last-played", (long long)e.lastPlayed);
    ini.setValue("last-slot", e.lastSlot);
    ini.setValue("character", e.character);
    ini.setValue("players", e.players);
    ini.endGroup();

    ini.writeIniFile();
}

bool Library_mostRecent(std::string &key, LibraryEpisode_t &out)
{
    IniProcessing ini;
    if(!ini.open(s_libraryFile()))
        return false;

    bool found = false;
    for(const std::string &group : ini.childGroups())
    {
        if(group == "library")
            continue;

        LibraryEpisode_t e;
        if(Library_get(group, e) && e.lastPlayed > 0 && (!found || e.lastPlayed > out.lastPlayed))
        {
            key = group;
            out = e;
            found = true;
        }
    }

    return found;
}

std::string Library_setting(const char *name, const std::string &def)
{
    IniProcessing ini;
    std::string v = def;

    if(ini.open(s_libraryFile()) && ini.beginGroup("library"))
    {
        ini.read(name, v, def);
        ini.endGroup();
    }

    return v;
}

void Library_setSetting(const char *name, const std::string &value)
{
    IniProcessing ini;
    ini.open(s_libraryFile());
    ini.beginGroup("library");
    ini.setValue(name, value);
    ini.endGroup();
    ini.writeIniFile();
}

std::string Library_smbx2DataDir()
{
    IniProcessing ini;
    std::string dir;

    if(ini.open(s_libraryFile()) && ini.beginGroup("library"))
    {
        ini.read("smbx2-data", dir, "");
        ini.endGroup();
    }

    return dir;
}

void Library_setSmbx2DataDir(const std::string &dir)
{
    IniProcessing ini;
    ini.open(s_libraryFile());
    ini.beginGroup("library");
    ini.setValue("smbx2-data", dir);
    ini.endGroup();
    ini.writeIniFile();
}
