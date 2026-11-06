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

#include <cstdio>
#include <cstring>
#include <cctype>

#ifndef _WIN32
#   include <dirent.h>
#   include <sys/stat.h>
#   include <sys/types.h>
#   include <sys/wait.h>
#   include <unistd.h>
#   include <spawn.h>
#   include <fcntl.h>
extern char **environ;
#endif

#include <AppPath/app_path.h>
#include <DirManager/dirman.h>
#include <Utils/files.h>
#include <Logger/logger.h>

#include "main/library_import.h"

// The importer touches only files it lists itself: lstat, so symlinks are
// never followed (a zip can hold one pointing anywhere) and never copied.

static std::string s_slash(std::string dir)
{
    if(!dir.empty() && dir.back() != '/')
        dir.push_back('/');
    return dir;
}

static std::string s_lower(const std::string &s)
{
    std::string l;
    for(char c : s)
        l.push_back((char)std::tolower((unsigned char)c));
    return l;
}

static bool s_endsWith(const std::string &s, const char *suffix)
{
    size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool LibraryImport_isZip(const std::string &path)
{
    return s_endsWith(s_lower(path), ".zip");
}

#ifdef _WIN32

bool LibraryImport_canUnzip()
{
    return false;
}

LibraryImportResult LibraryImport_run(const std::string &, LibraryImportProgress *progress)
{
    LibraryImportResult r;
    r.message = "Importing isn't available on this system yet.";
    if(progress)
        progress->finished = true;
    return r;
}

#else // POSIX: macOS, Linux, Android

struct DirEntries
{
    std::vector<std::string> files;
    std::vector<std::string> dirs;
};

static bool s_list(const std::string &dir, DirEntries &out)
{
    out = DirEntries();
    DIR *d = opendir(dir.c_str());
    if(!d)
        return false;

    while(struct dirent *e = readdir(d))
    {
        if(std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0)
            continue;

        struct stat st;
        std::string p = s_slash(dir) + e->d_name;
        if(lstat(p.c_str(), &st) != 0)
            continue;

        if(S_ISDIR(st.st_mode))
            out.dirs.push_back(e->d_name);
        else if(S_ISREG(st.st_mode))
            out.files.push_back(e->d_name);
        // symlinks and anything else: skipped
    }

    closedir(d);
    return true;
}

static bool s_isDir(const std::string &path)
{
    struct stat st;
    return lstat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool s_isFile(const std::string &path)
{
    struct stat st;
    return lstat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// removes a folder and everything in it, without following symlinks (only
// the importer's own folders; zips can store folders read-only, so each is
// made writable first)
static void s_removeTree(const std::string &dir)
{
    if(!s_isDir(dir))
        return;

    ::chmod(dir.c_str(), 0700);

    DirEntries e;
    if(!s_list(dir, e))
        return;

    for(const std::string &f : e.files)
        ::unlink((s_slash(dir) + f).c_str());

    // symlinks were left out of the listing; remove them as links
    DIR *d = opendir(dir.c_str());
    if(d)
    {
        while(struct dirent *de = readdir(d))
        {
            std::string p = s_slash(dir) + de->d_name;
            struct stat st;
            if(lstat(p.c_str(), &st) == 0 && S_ISLNK(st.st_mode))
                ::unlink(p.c_str());
        }
        closedir(d);
    }

    for(const std::string &sub : e.dirs)
        s_removeTree(s_slash(dir) + sub);

    ::rmdir(dir.c_str());
}

static int s_countFiles(const std::string &dir)
{
    DirEntries e;
    if(!s_list(dir, e))
        return 0;

    int n = (int)e.files.size();
    for(const std::string &sub : e.dirs)
        n += s_countFiles(s_slash(dir) + sub);
    return n;
}

static bool s_isSaveFile(const std::string &name)
{
    std::string l = s_lower(name);
    // SMBX's save<N>.sav, SMBX2's save<N>-ext.dat, TheXTech's own
    return l.compare(0, 4, "save") == 0
        && (s_endsWith(l, ".sav") || s_endsWith(l, ".dat") || s_endsWith(l, ".savx"));
}

// copies (or, from the zip's scratch folder, moves) a folder's files into
// another (made if needed); keeps the save files already at the destination
static bool s_copyTree(const std::string &from, const std::string &to, LibraryImportProgress *progress, int &failed, bool move)
{
    DirEntries e;
    if(!s_list(from, e))
        return false;

    if(!s_isDir(to) && !DirMan::mkAbsPath(to))
        return false;

    for(const std::string &f : e.files)
    {
        std::string dst = s_slash(to) + f;
        if(!(s_isSaveFile(f) && s_isFile(dst)))
        {
            std::string src = s_slash(from) + f;
            if(!(move && ::rename(src.c_str(), dst.c_str()) == 0) && !Files::copyFile(dst, src, true))
                failed++;
        }

        if(progress)
            progress->done++;
    }

    for(const std::string &sub : e.dirs)
        s_copyTree(s_slash(from) + sub, s_slash(to) + sub, progress, failed, move);

    return true;
}

bool LibraryImport_canUnzip()
{
#if defined(__ANDROID__)
    return false; // the Android app unpacks zips itself
#else
    return access("/usr/bin/unzip", X_OK) == 0;
#endif
}

// unpacks a zip with the system's unzip, run directly (no shell), leaving
// out Windows programs; unzip also refuses paths that leave the folder
static bool s_unzip(const std::string &zip, const std::string &into, std::string &error)
{
    std::vector<std::string> args =
    {
        "/usr/bin/unzip", "-qq", "-o", zip, "-d", into,
        "-x", "*.exe", "*.dll", "*.pdb", "*.bat", "*.msi",
    };

    std::vector<char *> argv;
    for(std::string &a : args)
        argv.push_back(&a[0]);
    argv.push_back(nullptr);

    // its messages ("excluded filename not matched", ...) aren't the player's
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);

    pid_t pid = 0;
    int spawned = posix_spawn(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);

    if(spawned != 0)
    {
        error = "The zip couldn't be opened (unzip didn't start).";
        return false;
    }

    int status = 0;
    if(waitpid(pid, &status, 0) < 0 || !WIFEXITED(status))
    {
        error = "Unpacking the zip stopped partway.";
        return false;
    }

    // unzip: 0 ok, 1 warnings (e.g. a skipped "../" path), 11 nothing matched
    int code = WEXITSTATUS(status);
    if(code > 1)
    {
        error = "The zip couldn't be unpacked (unzip error " + std::to_string(code) + ").";
        return false;
    }

    return true;
}

// what a folder holds
struct Found
{
    std::string smbx2Data;              // SMBX2's data folder, if any
    std::vector<std::string> episodes;  // episode folders
};

static bool s_hasWorldFile(const std::vector<std::string> &files)
{
    for(const std::string &f : files)
    {
        std::string l = s_lower(f);
        if(s_endsWith(l, ".wld") || s_endsWith(l, ".wldx"))
            return true;
    }
    return false;
}

static void s_find(const std::string &dir, int depth, Found &found)
{
    DirEntries e;
    if(!s_list(dir, e))
        return;

    // SMBX2's data folder: its episodes (worlds/) are imported as episodes
    if(found.smbx2Data.empty() && s_isFile(s_slash(dir) + "scripts/base/engine/main.lua"))
    {
        found.smbx2Data = s_slash(dir);
        if(s_isDir(s_slash(dir) + "worlds"))
            s_find(s_slash(dir) + "worlds", depth + 1, found);
        return;
    }

    if(s_hasWorldFile(e.files))
    {
        found.episodes.push_back(s_slash(dir));
        return; // levels' own folders are part of the episode
    }

    if(depth >= 6)
        return;

    for(const std::string &sub : e.dirs)
    {
        std::string l = s_lower(sub);
        if(l == "_templates" || l == "__macosx" || (!sub.empty() && sub[0] == '.'))
            continue;
        s_find(s_slash(dir) + sub, depth + 1, found);
    }
}

static std::string s_baseName(const std::string &dir)
{
    std::string d = dir;
    while(!d.empty() && d.back() == '/')
        d.pop_back();
    size_t slash = d.rfind('/');
    return slash == std::string::npos ? d : d.substr(slash + 1);
}

// SMBX2's folders the game reads (core mode, CLAUDE.md); never its programs
static const char *const s_smbx2Parts[] =
{
    "scripts", "graphics", "sound", "music", "costumes", "config",
    "PGE/configs/SMBX2-Integration", // extra settings' layouts (core/extrasettings.lua)
};

LibraryImportResult LibraryImport_run(const std::string &source, LibraryImportProgress *progress)
{
    LibraryImportResult r;
    std::string user = s_slash(AppPathManager::userAppDirSTD());
    std::string tmp = user + "import-tmp/";
    std::string root = source;
    bool fromZip = LibraryImport_isZip(source);

    auto finish = [&]() -> LibraryImportResult
    {
        if(fromZip)
            s_removeTree(tmp);
        if(progress)
            progress->finished = true;
        pLogInfo("Library import of %s: %s", source.c_str(), r.message.c_str());
        return r;
    };

    if(fromZip)
    {
        if(!LibraryImport_canUnzip())
        {
            r.message = "This system can't unpack zips here; unpack it first, then import the folder.";
            return finish();
        }

        if(progress)
            progress->setStage("Unpacking");

        s_removeTree(tmp);
        DirMan::mkAbsPath(tmp);

        std::string error;
        if(!s_unzip(source, tmp, error))
        {
            r.message = error;
            return finish();
        }

        root = tmp;
    }
    else if(!s_isDir(root))
    {
        r.message = "That isn't a folder or a zip.";
        return finish();
    }

    if(progress)
        progress->setStage("Looking inside");

    Found found;
    s_find(root, 0, found);

    if(found.smbx2Data.empty() && found.episodes.empty())
    {
        r.message = "No episodes or SMBX2 data were found in it.";
        return finish();
    }

    // count what will be copied, for the progress bar
    int total = 0;
    if(!found.smbx2Data.empty())
    {
        for(const char *part : s_smbx2Parts)
            total += s_countFiles(found.smbx2Data + part);
    }
    for(const std::string &ep : found.episodes)
        total += s_countFiles(ep);

    if(progress)
        progress->total = total;

    int failed = 0;

    if(!found.smbx2Data.empty())
    {
        if(progress)
            progress->setStage("Copying SMBX2's data");

        // into a new folder first, so a half-copied one never replaces a good one
        std::string next = user + "smbx2.importing/";
        s_removeTree(next);

        for(const char *part : s_smbx2Parts)
        {
            if(s_isDir(found.smbx2Data + part))
                s_copyTree(found.smbx2Data + part, next + part, progress, failed, fromZip);
        }

        if(s_isFile(next + "scripts/base/engine/main.lua"))
        {
            std::string dest = user + "smbx2";
            std::string old = user + "smbx2.old";
            s_removeTree(old);
            if(s_isDir(dest))
                ::rename(dest.c_str(), old.c_str());
            if(::rename(next.substr(0, next.size() - 1).c_str(), dest.c_str()) == 0)
            {
                s_removeTree(old);
                r.smbx2Data = true;
            }
            else
            {
                if(s_isDir(old))
                    ::rename(old.c_str(), dest.c_str());
                s_removeTree(next);
            }
        }
        else
            s_removeTree(next);
    }

    std::string worlds = s_slash(AppPathManager::userWorldsRootDir());
    for(const std::string &ep : found.episodes)
    {
        std::string name = s_baseName(ep);
        if(name.empty() || name == "import-tmp")
            name = "Imported episode";

        if(progress)
            progress->setStage("Copying " + name);

        std::string dest = worlds + name;
        if(s_copyTree(ep, dest, progress, failed, fromZip))
            r.episodes.push_back(s_slash(dest));
    }

    r.ok = r.smbx2Data || !r.episodes.empty();

    std::string what;
    if(r.smbx2Data)
        what = "SMBX2's data";
    if(!r.episodes.empty())
    {
        if(!what.empty())
            what += " and ";
        what += (r.episodes.size() == 1) ? ("\"" + s_baseName(r.episodes[0]) + "\"")
                                         : (std::to_string(r.episodes.size()) + " episodes");
    }

    if(!r.ok)
        r.message = "Nothing could be copied; check there's enough free space.";
    else if(failed > 0)
        r.message = "Imported " + what + ", but " + std::to_string(failed) + " files couldn't be copied.";
    else
        r.message = "Imported " + what + ".";

    return finish();
}

#endif // _WIN32
