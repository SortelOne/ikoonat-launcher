--[[
  Adapted from SMBX2's base/engine/lockdown.lua, by the SMBX2 team
  (https://codehaus.moe); much of the sandboxing below is theirs, with
  changes for this engine. Credit to them.

  smbx2compat core: replaces SMBX2's base/engine/lockdown.lua, which opens and
  reads files through LunaDll.dll. Same globals and the same sandboxing; plain
  Lua io underneath, with every path going through boot.lua's resolver
  (Windows separators, case-insensitive names, our overrides).

  Not reproduced: LunaLua's path validator, which limits writes to the episode
  and SMBX2 folders.
]]

local core = __smbx2core
local lockdown = {}
lockdown.defaultLoadEnv = {}

------------------
-- Lock down os --
------------------
if os ~= nil then
    local nativeOS = os
    local newOS = {}
    newOS.clock = nativeOS.clock
    newOS.date = nativeOS.date
    newOS.time = nativeOS.time
    newOS.difftime = nativeOS.difftime
    newOS.exit = function() error("Shutdown") end

    os = newOS
    _G.os = newOS
end

------------------
-- Lock down io --
------------------
local openFile
do
    local nativeIO = io
    local newIO = {}

    -- SMBX2 writes reports (debugtools' warning list) into <SMBX>/logs, which
    -- our copy of SMBX2's data doesn't have; they go to TheXTech's log folder
    local smbxLogs = string.lower(core.normalize(core.smbxPath .. "/logs/"))
    local function logsRedirect(path)
        path = core.normalize(path)
        if string.lower(string.sub(path, 1, #smbxLogs)) == smbxLogs then
            return core.normalize(__xtech_logsDir .. "/" .. string.sub(path, #smbxLogs + 1))
        end
        return nil
    end

    function openFile(path, mode)
        if not path then
            error("bad argument #1 to 'open' (string expected, got nil)")
        end
        local real = logsRedirect(path) or core.resolve(path) or core.normalize(path)
        local f, err, errno = nativeIO.open(real, mode or "r")
        if mode and string.find(mode, "[wa+]") then
            xtech_misc_pathChanged(real) -- core.resolve's folder listing
        end
        if not f then
            return nil, "Couldn't open " .. tostring(path) .. ": " .. tostring(err), errno
        end
        return f
    end

    newIO.close = nativeIO.close
    newIO.read = nativeIO.read
    newIO.flush = nativeIO.flush
    newIO.type = nativeIO.type
    newIO.open = openFile
    newIO.lines = function(path)
        -- Return stdin line iterator if path is nil
        if not path then
            return nativeIO.lines()
        end

        local f, errorMsg, errno = openFile(path, "r")
        if f then
            return f:lines()
        end
        return nil, errorMsg, errno
    end

    io = newIO
    _G.io = newIO
end

--------------------------------
-- Lock down AsyncHTTPRequest --
--------------------------------
AsyncHTTPRequest = nil
_G.AsyncHTTPRequest = nil

---------------------
-- Lock down debug --
---------------------
if debug ~= nil then
    local nativeDebug = debug
    local newDebug = {}

    -- Safe getfenv that only accepts 1 for an input
    function newDebug.getfenv(o)
        if (o == 1) then
            return nativeDebug.getfenv(2)
        end
        error("Only allowed to call getfenv(n) with n==1")
    end

    newDebug.getmetatable = nativeDebug.getmetatable
    newDebug.traceback = nativeDebug.traceback
    newDebug.getinfo = nativeDebug.getinfo

    debug = newDebug
    _G.debug = newDebug
    getfenv = newDebug.getfenv
    _G.getfenv = newDebug.getfenv

    function unpack_here(tbl)
        local ctx = nativeDebug.getfenv(2)
        for k, v in pairs(tbl) do
            ctx[k] = v
        end
    end
end

----------------------------------
-- Reading and writing whole files --
----------------------------------
local readFile = core.readFile

local function readFileLines(path)
    local data = readFile(path)
    if (data == nil) then
        return nil
    end

    local ret = {}
    for line in string.gmatch(data, "([^\n\r]*)\r?\n?") do
        ret[#ret+1] = line
    end
    ret[#ret] = nil
    return ret
end

local function exists(path)
    return core.resolve(path) ~= nil
end

local function writeFile(path, data)
    path = tostring(path)
    data = tostring(data)
    local f, err = openFile(path, "wb")
    assert(f ~= nil, "Error writing file \"" .. path .. "\": " .. tostring(err))
    f:write(data)
    f:close()
end

io.readFile = readFile
io.readFileLines = readFileLines
io.exists = exists
io.writeFile = writeFile
lockdown.readFile = readFile

-----------------------
-- Lock down globals --
-----------------------
do
    local nativeLoad = load
    _G.setfenv = nil
    _G.newproxy = nil

    local function safeLoad(s, chunkname, mode, env)
        if env == nil then
            env = lockdown.defaultLoadEnv
        end
        return nativeLoad(s, chunkname, "t", env) -- text chunks only
    end

    local function safeLoadFile(filename, mode, env)
        local s = readFile(filename)
        if s == nil then
            return nil, "cannot open " .. tostring(filename)
        end
        return safeLoad(s, "@" .. filename, mode, env)
    end

    local function safeDoFile(filename)
        local func, err = safeLoadFile(filename)
        if (func == nil) then
            error("Error: " .. err)
        end
        return func()
    end

    _G.load = safeLoad
    _G.dofile = safeDoFile
    _G.loadfile = safeLoadFile
    _G.loadstring = safeLoad
end

return lockdown
