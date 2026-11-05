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

#include <cstdlib>
#include <cstring>

#include "globals.h"
#include "main/smbx2_ids.h"

#include <Utils/files.h>
#include <Logger/logger.h>

bool g_smbx2Ids = false;

void SMBX2Ids_Detect()
{
    // the same test as core mode's (xtech_lua_main.cpp, findSmbx2Core)
    const char *mode = std::getenv("SMBX2COMPAT_MODE");
    bool forcedShim = mode && std::strcmp(mode, "shim") == 0;

    bool was = g_smbx2Ids;
    g_smbx2Ids = !forcedShim && Files::fileExists(AppPath + "smbx2/scripts/base/engine/main.lua");

    if(g_smbx2Ids != was)
        pLogInfo("smbx2compat: %s object IDs", g_smbx2Ids ? "SMBX2" : "TheXTech");
}

std::string SMBX2Ids_GfxDir(const char *category)
{
    if(!g_smbx2Ids)
        return std::string();

    return AppPath + "smbx2/graphics/" + category;
}
