--[[
  smbx2compat core: the host half of LunaLua, so SMBX2's own scripts run as-is.

  The engine (core mode in script/src/xtech_lua_main.cpp) creates a fresh Lua
  state for every level, sets __xtech_smbx2Path (SMBX2's data folder, the one
  holding scripts/ and graphics/), __xtech_coreDir (this folder) and
  __xtech_isOverworld, __xtech_episodePath and __xtech_levelFile (e.g.
  "1-1.lvlx"), then runs this file. We:
    1. define what LunaLua's C++ defines before main.lua runs (constants.lua,
       host.lua, classes.lua; extrasettings.lua for custom params),
    2. send every file SMBX2 reads through one resolver (resolve below),
    3. answer ffi.load("LunaDll.dll") with lunadll.lua, a Lua table standing in
       for the DLL's exports,
    4. run SMBX2's scripts/base/engine/main.lua, and
    5. define the __xtech_on* functions the engine calls every frame.

  Files in override/ replace the SMBX2 file at the same path under scripts/;
  patches.lua lists small source edits applied to SMBX2 files as they load.
]]

local smbxPath = __xtech_smbx2Path
local coreDir = __xtech_coreDir
local scriptsDir = smbxPath .. "/scripts/"
local overrideDir = coreDir .. "/override/"

-- main.lua's lockdown replaces or removes these; keep the originals
local nativeIO = io
local loadstring = loadstring
local setfenv = setfenv
local traceback = debug.traceback

local core = {}
_G.__smbx2core = core


-------------
-- Logging --
-------------

-- Per-frame code repeats the same message every tick; log each one once
local logged, loggedCount = {}, 0
local function logOnce(msg)
    if logged[msg] then
        return false
    end
    if loggedCount < 1000 then -- bound memory if a script produces endless unique messages
        logged[msg] = true
        loggedCount = loggedCount + 1
    end
    return true
end

function core.log(msg)
    xtech_misc_log("[smbx2core] " .. msg)
end

function core.warn(msg)
    if logOnce(msg) then
        xtech_misc_logWarn("[smbx2core] " .. msg)
    end
end

-- Something SMBX2 asked for that we don't provide yet. Listed again at level exit.
local missing = {}
core.missing = missing
function core.missingAPI(what)
    if missing[what] then
        return
    end
    missing[what] = true
    missing[#missing + 1] = what
    xtech_misc_logWarn("[smbx2core] missing: " .. what)
end


-----------
-- Files --
-----------

-- SMBX2 was written for Windows and checks "is this path absolute?" by
-- looking for a drive letter (^%a:[/\\]) in several places. So paths we hand
-- to scripts get a virtual "C:" in front ("C:/Users/..."), and normalize()
-- drops it again on the way back in.
local function normalize(path)
    path = string.gsub(path, "[\\/]+", "/")
    if string.find(path, "^%a:/") then
        path = string.sub(path, 3)
    end
    return path
end
core.normalize = normalize

-- An engine path as scripts see it
local function scriptPath(path)
    if string.sub(path, 1, 1) == "/" then
        return "C:" .. path
    end
    return path
end
core.scriptPath = scriptPath

local function startsWithCI(s, prefix)
    return string.lower(string.sub(s, 1, #prefix)) == string.lower(prefix)
end

local function isFile(path)
    local f = nativeIO.open(path, "rb")
    if not f then
        return false
    end
    -- Directories open fine on macOS/Linux, then fail to read
    local data, err = f:read(1)
    f:close()
    return data ~= nil or err == nil
end

local function isDir(path)
    local f = nativeIO.open(path, "rb")
    if not f then
        return false
    end
    local data, err = f:read(1)
    f:close()
    return data == nil and err ~= nil
end
core.isDir = isDir

-- Folders whose files are matched case-insensitively, longest first. SMBX2 was
-- written for Windows: it requires "classExpander" but ships "classexpander.lua".
local roots = {}
function core.addRoot(dir)
    dir = normalize(dir)
    if string.sub(dir, -1) ~= "/" then
        dir = dir .. "/"
    end
    for _, r in ipairs(roots) do
        if r == dir then
            return
        end
    end
    roots[#roots + 1] = dir
    table.sort(roots, function(a, b) return #a > #b end)
end

-- Where this level's files live: SMBX2's data folder, the episode folder, and
-- the level's own folder (named like the level file, without the extension)
core.smbxPath = smbxPath
core.episodePath = normalize(__xtech_episodePath)
core.customPath = core.episodePath .. string.gsub(__xtech_levelFile, "%.[^.]*$", "") .. "/"
core.addRoot(smbxPath)
core.addRoot(core.episodePath)
core.addRoot(core.customPath)

-- Turns a path from SMBX2 code into a real file path, or nil if there's no such
-- file. The second result is true when the file is one of our overrides.
local function resolve(path)
    path = normalize(path)
    if startsWithCI(path, scriptsDir) then
        local override = xtech_misc_resolveFileCI(overrideDir, string.sub(path, #scriptsDir + 1))
        if override ~= "" then
            return override, true
        end
    end
    if isFile(path) then
        return path, false
    end
    for _, root in ipairs(roots) do
        if startsWithCI(path, root) then
            local found = xtech_misc_resolveFileCI(root, string.sub(path, #root + 1))
            if found ~= "" then
                return found, false
            end
            return nil
        end
    end
    return nil
end
core.resolve = resolve

local function readRaw(path)
    local f = nativeIO.open(path, "rb")
    if not f then
        return nil
    end
    local data = f:read("*a")
    f:close()
    return data
end

local function runCoreFile(name)
    local path = coreDir .. "/" .. name
    local data = readRaw(path) or error("smbx2core file missing: " .. path)
    return assert(loadstring(data, "@smbx2core/" .. name))()
end

-- patches["base/engine/require.lua"] = { {find, replace, why}, ... }
local patches = runCoreFile("patches.lua")

local function applyPatches(path, data)
    path = normalize(path)
    if not startsWithCI(path, scriptsDir) then
        return data
    end
    local list = patches[string.lower(string.sub(path, #scriptsDir + 1))]
    if not list then
        return data
    end
    for _, p in ipairs(list) do
        local s, e = string.find(data, p[1], 1, true)
        local replacement = p[2]
        if not s then
            -- Many SMBX2 files have Windows line endings; patches are written with \n
            s, e = string.find(data, (string.gsub(p[1], "\n", "\r\n")), 1, true)
            replacement = string.gsub(p[2], "\n", "\r\n")
        end
        if s then
            data = string.sub(data, 1, s - 1) .. replacement .. string.sub(data, e + 1)
        else
            core.warn("patch did not apply to " .. path .. " (" .. p[3] .. ")")
        end
    end
    return data
end

-- What SMBX2 knows as io.readFile (LunaLuaCachedReadFile in LunaLua)
function core.readFile(path)
    local real, isOverride = resolve(path)
    if not real then
        return nil
    end
    local data = readRaw(real)
    if data and not isOverride then
        data = applyPatches(path, data)
    end
    return data
end

-- main.lua dofile()s lockdown.lua by its path, before lockdown replaces dofile
-- with a version that reads through io.readFile; route it here so our
-- override is the one that runs.
function _G.dofile(path)
    local data = core.readFile(path) or error("cannot open " .. tostring(path))
    return assert(loadstring(data, "@" .. normalize(path)))()
end

function _G.loadfile(path, mode, env)
    local data = core.readFile(path)
    if not data then
        return nil, "cannot open " .. tostring(path)
    end
    local f, err = loadstring(data, "@" .. normalize(path))
    if f and env then
        setfenv(f, env)
    end
    return f, err
end


-- Functions to run once each frame has been drawn (core.onFrameEnd(f))
local frameEndFuncs = {}
function core.onFrameEnd(f)
    frameEndFuncs[#frameEndFuncs + 1] = f
end
function core.endFrame()
    for _, f in ipairs(frameEndFuncs) do
        f()
    end
end


---------------------------
-- Host globals and DLL --
---------------------------

isOverworld = __xtech_isOverworld and true or false

function getSMBXPath()
    return scriptPath(smbxPath)
end

runCoreFile("constants.lua")
runCoreFile("host.lua")
runCoreFile("classes.lua")
core.extraSettings = runCoreFile("extrasettings.lua")

do
    local ffi = require("ffi")
    local nativeLoad = ffi.load

    -- ffi_misc.lua builds Misc.clock on Windows' high-resolution counter
    local kernel32 = {
        QueryPerformanceFrequency = function(out)
            out[0] = 1000000000
            return 1
        end,
        QueryPerformanceCounter = function(out)
            out[0] = math.floor(xtech_misc_clock() * 1000000000)
            return 1
        end,
    }

    local libraries = {
        ["lunadll.dll"] = runCoreFile("lunadll.lua"),
        ["kernel32.dll"] = kernel32,
    }
    ffi.load = function(name, ...)
        local lib = libraries[string.lower(name)]
        if lib then
            return lib
        end
        return nativeLoad(name, ...)
    end
end


------------------
-- Run main.lua --
------------------

do
    local mainPath = scriptsDir .. "base/engine/main.lua"
    local src = core.readFile(mainPath) or error("SMBX2 main.lua not found at " .. mainPath)
    local chunk = assert(loadstring(src, "=main.lua"))
    local ok, err = xpcall(chunk, traceback)
    if not ok then
        error(err, 0)
    end
    if __isLuaError then
        error("main.lua flagged an error (logged above)", 0)
    end
end

-- The engine passes the episode folder as it knows it
do
    local onInit = __onInit
    function __onInit(episodePath, lvlName)
        return onInit(scriptPath(normalize(episodePath)), lvlName)
    end
end

if string.sub(core.episodePath, 1, 1) ~= "/" then
    core.warn("episode path isn't absolute (" .. core.episodePath .. "); SMBX2 expects it to be")
end
core.log("main.lua done; level " .. __xtech_levelFile .. " in " .. core.episodePath)


---------------------
-- Per-frame hooks --
---------------------

-- LunaLua sends events as objects; main_events.lua reads these fields.
-- Like LunaLua, nothing is sent before onStart.
local started = false
local tickEndReady, drawEndReady = false, false

local function sendEvent(name, cancellable, ...)
    if not started then
        return false
    end
    local e = {
        eventName = name,
        directEventName = name,
        loopable = false,
        cancellable = cancellable,
        cancelled = false,
    }
    __callEvent(e, ...)
    return e.cancelled
end
core.sendEvent = sendEvent

-- Called by the engine at the top of each game frame, before input is read
function __xtech_onLoop()
    if not started then
        started = true
        sendEvent("onStart", false)
    end
    sendEvent("onLoop", false)
    __doEventQueue()
    -- LunaLua sends these right after SMBX reads input; TheXTech calls us just
    -- before, so scripts see last frame's input. TODO: an engine hook after input.
    sendEvent("onInputUpdate", false)
    sendEvent("onTick", false)
    tickEndReady = true
end

function __xtech_onRenderStart()
    if tickEndReady then
        tickEndReady = false
        sendEvent("onTickEnd", false)
    end
    sendEvent("onDraw", false)
    drawEndReady = true
end

function __xtech_onRenderEnd()
    if drawEndReady then
        drawEndReady = false
        sendEvent("onDrawEnd", false)
    end
    core.endFrame()
end

function __xtech_onExit()
    -- TODO: the real exit type (LunaLua passes GM_LEVEL_EXIT_TYPE)
    sendEvent("onExitLevel", false, 0)
    sendEvent("onExit", false)
    if #missing > 0 then
        xtech_misc_logWarn("[smbx2core] " .. #missing .. " missing APIs this level: " .. table.concat(missing, ", "))
    end
end
