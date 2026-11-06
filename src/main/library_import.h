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
#ifndef LIBRARY_IMPORT_H
#define LIBRARY_IMPORT_H

// smbx2compat: the episode library's importer. Given a folder or a zip, it
// finds what's inside and copies it into the user folder:
// - SMBX2's data (a folder with scripts/base/engine/main.lua): only what the
//   game reads (scripts, graphics, sound, music, costumes, config and
//   PGE/configs) into <user>/smbx2, never SMBX2's programs;
// - episodes (folders with a .wld/.wldx; SMBX2's _templates skipped) into
//   <user>/worlds/<name>, keeping the save files already there.
// Zips are unpacked first with the system's unzip (macOS, Linux); Android's
// app unpacks them itself and passes the folder.

#include <string>
#include <vector>
#include <atomic>
#include <mutex>

struct LibraryImportProgress
{
    std::atomic<int> done{0};
    std::atomic<int> total{0};
    std::atomic<bool> finished{false};

    std::mutex lock;
    //! what it's doing now ("Unpacking", "Copying SMBX2's data", ...)
    std::string stage;

    void setStage(const std::string &s)
    {
        std::lock_guard<std::mutex> g(lock);
        stage = s;
    }

    std::string getStage()
    {
        std::lock_guard<std::mutex> g(lock);
        return stage;
    }
};

struct LibraryImportResult
{
    bool ok = false;
    //! a sentence for the player: what was imported, or what went wrong
    std::string message;
    //! the folders of the episodes it imported (under <user>/worlds)
    std::vector<std::string> episodes;
    bool smbx2Data = false;
};

//! True when the path names a zip file
bool LibraryImport_isZip(const std::string &path);

//! True when this build can unpack zips itself
bool LibraryImport_canUnzip();

/*!
 * \brief Imports a folder or zip. Slow (copies files): run it on a thread
 * and watch progress from the UI.
 */
LibraryImportResult LibraryImport_run(const std::string &source, LibraryImportProgress *progress);

#endif // LIBRARY_IMPORT_H
