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

  The loading screen's state (xtech_lua_loadscreen.cpp) runs this file too,
  with __xtech_loadScreen set: it skips host.lua, classes.lua and
  extrasettings.lua, and runs loadscreen.lua in place of steps 4 and 5.
]]

local smbxPath = __xtech_smbx2Path
local coreDir = __xtech_coreDir
-- The loading screen's own state (xtech_lua_loadscreen.cpp): set up the same
-- way, then loadscreen.lua runs SMBX2's loading screen instead of main.lua
local loadScreen = __xtech_loadScreen
local scriptsDir = smbxPath .. "/scripts/"
local overrideDir = coreDir .. "/override/"

-- main.lua's lockdown replaces or removes these; keep the originals
local nativeIO = io
local loadstring = loadstring
local setfenv = setfenv
local traceback = debug.traceback

local core = {}
_G.__smbx2core = core

-- One 8 MB machine-code area for the JIT, set before anything is compiled
-- (main.lua's jit.opt.start sets the other limits, maxmcode=8192 among them).
-- With LuaJIT's 64 KB areas, every new area has to land within branch range
-- (+-64 MB on arm64) of the others; as the engine loads graphics that space
-- fills up, a new area can't be placed, and LuaJIT throws all compiled code
-- away (an MCODEAL flush). In SMATRS 1-1 that happened up to every 2-3 frames.
-- SMBX2 runs as 32-bit Windows code, where this can't happen.
jit.opt.start("sizemcode=8192", "maxmcode=8192")
local jitOpt = jit.opt -- main.lua's lockdown hides jit

-- SMBX2COMPAT_PROFILE turns on the timing probe (profile.lua); ffi_mem.lua
-- counts mem() calls into profileMemCalls when it's set
core.profileSpec = os.getenv("SMBX2COMPAT_PROFILE")
if core.profileSpec and core.profileSpec ~= "" and core.profileSpec ~= "0" then
    core.profileMemCalls = {get = 0, set = 0, string = 0}
    if string.find(core.profileSpec, "memcallers", 1, true) then
        core.profileMemCallers = {}
        core.getinfo = debug.getinfo -- main.lua's lockdown takes debug away
    end
    core.jitUtil = require("jit.util") -- main.lua takes jit out of package.loaded
else
    core.profileSpec = nil
end
-- Testing: SMBX2COMPAT_SHOTS=300,900 saves screenshots of those frames (see the end)
core.shotsSpec = os.getenv("SMBX2COMPAT_SHOTS")
core.configSpec = os.getenv("SMBX2COMPAT_CONFIG") -- TheXTech options for the run (see the end)
core.jitOptSpec = os.getenv("SMBX2COMPAT_JITOPT")
-- Debugging: SMBX2COMPAT_DEBUG=/path/file.lua runs that file once the level's
-- scripts have loaded (before onStart), with core passed as its argument.
-- Without it (Android has no environment to set), smbx2compat-debug.lua in the
-- user folder, if there is one.
core.debugSpec = os.getenv("SMBX2COMPAT_DEBUG")
if (core.debugSpec == nil or core.debugSpec == "") and __xtech_userDir then
    local path = string.gsub(__xtech_userDir, "/*$", "/") .. "smbx2compat-debug.lua"
    local f = io.open(path, "rb")
    if f then
        f:close()
        core.debugSpec = path
    end
end
-- for debug files, which SMBX2's lockdown keeps from require("ffi") and os
core.ffi = require("ffi")
core.getenv = os.getenv
-- the screen's size now (the size a script asked for, else TheXTech's), for
-- patches.lua's loading screen edit
core.screenSize = xtech_gfx_getFramebufferSize


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

-- Folder listings kept by the engine for the level (xtech_misc_pathKind), -1
-- where it can't list; opening files to probe them took ~1 s of each level start
local pathKind = xtech_misc_pathKind

local function isFile(path)
    local kind = pathKind(path)
    if kind >= 0 then
        return kind == 1
    end
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
    local kind = pathKind(path)
    if kind >= 0 then
        return kind == 2
    end
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

-- While the level loads, the loading screen draws a frame here whenever one is
-- due: every file the level's scripts look for or load passes through
local loadScreenUpdate = (not loadScreen) and xtech_misc_loadScreenUpdate or function() end

-- Turns a path from SMBX2 code into a real file path, or nil if there's no such
-- file. The second result is true when the file is one of our overrides.
local function resolve(path)
    loadScreenUpdate()
    path = normalize(path)
    -- SMBX2 runs in its data folder, so relative paths are from there (SMATRS's
    -- loading screen looks for "scripts/shaders/..." that way)
    if string.sub(path, 1, 1) ~= "/" then
        path = smbxPath .. "/" .. path
    end
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

-- Kept by the engine across levels while the file stays the same (as
-- LunaLua's cached reads); nil where it can't, then read here
local readFileCached = xtech_misc_readFileCached

local function readRaw(path)
    local data = readFileCached(path)
    if data then
        return data
    end
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
if not loadScreen then -- LunaLua's loading screen state has none of these
    runCoreFile("host.lua")
    runCoreFile("classes.lua")
    core.extraSettings = runCoreFile("extrasettings.lua")
end
core.glsl = runCoreFile("glsl.lua")
core.glsl.types = runCoreFile("glsltypes.lua") -- int -> float conversions for OpenGL ES

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

-- Block/BGO ID maps as byte tables for the engine's ID searches (the iterate
-- patches to ffi_block.lua/ffi_bgo.lua in patches.lua): filter[id] is 1 for
-- each ID the map has a true value for. One table per map, refilled when a
-- loop over it starts (i == 0), since scripts can change a map between loops.
do
    local ffi = require("ffi")
    local size = 2001 -- SMBX2's IDs go up to 2000 (blocks)
    local filters = setmetatable({}, {__mode = "k"})
    core.idFilterSize = size

    function core.idFilter(map, i)
        local filter = filters[map]
        if filter and i ~= 0 then
            return filter
        end
        if filter then
            ffi.fill(filter, size)
        else
            filter = ffi.new("uint8_t[?]", size)
            filters[map] = filter
        end
        for id, v in pairs(map) do
            if v and type(id) == "number" and id >= 0 and id < size and id % 1 == 0 then
                filter[id] = 1
            end
        end
        return filter
    end
end


if loadScreen then
    runCoreFile("loadscreen.lua")
    return
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

-- SMBX2COMPAT_JITOPT="hotloop=56,loopunroll=60" overrides main.lua's JIT
-- settings, for trying them out with the profiler
do
    local spec = core.jitOptSpec
    if spec and spec ~= "" then
        local opts = {}
        for opt in string.gmatch(spec, "[^,]+") do
            opts[#opts + 1] = opt
        end
        jitOpt.start(unpack(opts))
        core.log("JIT options: " .. spec)
    end
end

-- The engine passes the episode folder as it knows it
do
    local onInit = __onInit
    function __onInit(episodePath, lvlName)
        local ret = onInit(scriptPath(normalize(episodePath)), lvlName)
        if core.debugSpec and core.debugSpec ~= "" then
            core.log("running debug file " .. core.debugSpec)
            local chunk, err = loadstring(core.readFile(core.debugSpec) or "", "=" .. core.debugSpec)
            local ok
            if chunk then
                ok, err = xpcall(chunk, traceback, core)
            end
            if not ok then
                core.warn("debug script: " .. tostring(err))
            end
        end
        return ret
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

-- Players whose section changed: LunaLua sends them onSectionChange right
-- after the event during which that happened (LunaLuaMain.h callEvent)
local sectionChanges = {}
local sendingSectionChanges = false

function core.queueSectionChange(playerIdx)
    if sendingSectionChanges then
        return
    end
    for _, p in ipairs(sectionChanges) do
        if p == playerIdx then
            return
        end
    end
    sectionChanges[#sectionChanges + 1] = playerIdx
end

local callEventObj

local function sendSectionChanges()
    sendingSectionChanges = true
    local players = sectionChanges
    sectionChanges = {}
    for _, p in ipairs(players) do
        callEventObj({eventName = "onSectionChange", directEventName = "onSectionChange",
            loopable = false, cancellable = false, cancelled = false}, xtech_player_get(p).Section, p)
    end
    sendingSectionChanges = false
end

-- Returns whether a script cancelled the event
function callEventObj(e, ...)
    if not started then
        return false
    end
    __callEvent(e, ...)
    if #sectionChanges > 0 and not sendingSectionChanges then
        sendSectionChanges()
    end
    return e.cancelled
end

function core.isStarted()
    return started
end

local function sendEvent(name, cancellable, ...)
    return callEventObj({
        eventName = name,
        directEventName = name,
        loopable = false,
        cancellable = cancellable,
        cancelled = false,
    }, ...)
end
core.sendEvent = sendEvent

-- onStart, followed by onSectionChange for each player (LunaLua's triggerOnStart)
local function start()
    if started then
        return
    end
    started = true
    for p = 1, xtech_player_count() do
        core.queueSectionChange(p)
    end
    sendEvent("onStart", false)
end

-- Called by the engine at the top of each game frame, before input is read
function __xtech_onLoop()
    start()
    sendEvent("onLoop", false)
    __doEventQueue()
end

-- Right after the engine reads input (LunaLua's hookInputUpdate), so scripts
-- see this frame's keys and can change them before the players move
function __xtech_onInputUpdate()
    core.updateRawKeys()
    sendEvent("onInputUpdate", false)
    sendEvent("onTick", false)
    tickEndReady = true
    return false
end

-- The level renderer's events, in LunaLua's order: onTickEnd, onCameraUpdate for
-- each camera, onDraw, then per camera onCameraDraw, (drawing), onHUDUpdate and
-- onHUDDraw before the HUD; onDrawEnd once all are drawn
function __xtech_onRenderStart()
    if tickEndReady then
        tickEndReady = false
        sendEvent("onTickEnd", false)
    end
end

function __xtech_onCameraUpdate(camIdx)
    sendEvent("onCameraUpdate", false, camIdx)
end

function __xtech_onDraw()
    sendEvent("onDraw", false)
    drawEndReady = true
end

function __xtech_onCameraDraw(camIdx, maxCamIdx)
    sendEvent("onCameraDraw", false, camIdx, maxCamIdx)
end

function __xtech_onHUDDraw(camIdx)
    sendEvent("onHUDUpdate", false, camIdx)
    sendEvent("onHUDDraw", false, camIdx)
end

function __xtech_onRenderEnd()
    if drawEndReady then
        drawEndReady = false
        sendEvent("onDrawEnd", false)
    end
    core.endFrame()
end

-- The engine removed an NPC and moved its last one into the slot (0-based
-- indices); ffi_npc.lua moves its NPC objects, with their data, to match
function __xtech_onNPCRearrange(newIdx, oldIdx)
    sendEvent("onPostNPCRearrangeInternal", false, newIdx, oldIdx)
end

-- A P-switch puts a new block into a slot it freed (1-based, as SMBX2's
-- BlockCache); ffi_block.lua drops its Block object for the old block
function __xtech_onBlockReuse(idx)
    sendEvent("onBlockInvalidateForReuseInternal", false, idx)
end

-- Engine events about one object (LunaLua's RuntimeHookHooks.cpp). Each
-- returns whether a script cancelled it; main_events.lua converts the indices
-- into objects and sends the onPost* events itself.
function __xtech_onNPCKill(npcIdx, killReason)
    return sendEvent("onNPCKill", true, npcIdx, killReason)
end

function __xtech_onNPCHarm(npcIdx, harmType, culpritIdx)
    return sendEvent("onNPCHarm", true, npcIdx, harmType, culpritIdx)
end

function __xtech_onNPCCollect(npcIdx, playerIdx)
    return sendEvent("onNPCCollect", true, npcIdx, playerIdx)
end

function __xtech_onBlockHit(blockIdx, fromUpSide, playerIdx)
    return sendEvent("onBlockHit", true, blockIdx, fromUpSide, playerIdx)
end

function __xtech_onBlockRemove(blockIdx, makeEffects)
    return sendEvent("onBlockRemove", true, blockIdx, makeEffects, false)
end

function __xtech_onPOW()
    return sendEvent("onPOW", true)
end

function __xtech_onPlayerHarm(playerIdx)
    return sendEvent("onPlayerHarm", true, playerIdx)
end

function __xtech_onPlayerKill(playerIdx)
    return sendEvent("onPlayerKill", true, playerIdx)
end

function __xtech_onWarpEnter(warpIdx, playerIdx)
    return sendEvent("onWarpEnter", true, warpIdx, playerIdx)
end

function __xtech_onWarp(warpIdx, playerIdx, sectionChanged)
    if sectionChanged then
        core.queueSectionChange(playerIdx)
    end
    sendEvent("onWarp", false, warpIdx, playerIdx)
    return false
end

-- A level event: LunaLua sends onStart just before "Level - Start", then
-- onEventDirect (cancellable) now and onEvent from the next frame's event queue
function __xtech_onEvent(name)
    if name == "Level - Start" then
        start()
    end
    return callEventObj({eventName = "onEvent", directEventName = "onEventDirect",
        loopable = true, cancellable = true, cancelled = false}, name)
end

-- A message box is about to open; npcIdx only for an NPC's (LunaLua's
-- runtimeHookMsgbox / runtimeHookNpcMsgbox)
function __xtech_onMessageBox(text, playerIdx, npcIdx)
    return sendEvent("onMessageBox", true, text, playerIdx, npcIdx)
end

-- A player opened the pause menu (LunaLua sends it from the same hook)
function __xtech_onPause(playerIdx)
    return sendEvent("onPause", true, playerIdx)
end

function __xtech_onColorSwitch(color)
    sendEvent("onColorSwitch", false, color)
    return false
end

function __xtech_onNPCGenerated(generatorIdx, npcIdx)
    sendEvent("onNPCGenerated", false, generatorIdx, npcIdx)
    return false
end

-- loc: the explosion's position and size ({x, y, speedX, speedY, width, height})
function __xtech_onExplosion(loc, kind, playerIdx)
    return sendEvent("onExplosionInternal", true, loc, kind, playerIdx)
end

function __xtech_onNPCTransform(npcIdx, oldID, cause)
    sendEvent("onNPCTransform", false, npcIdx, oldID, cause)
    return false
end

-- A key went down or up (Windows virtual-key code). LunaLua sends these
-- loopable, so the *Direct names run now and the plain ones next frame.
-- onKeyboardKeyPress/Release carry the key's character (MapVirtualKey: capital
-- letters and digits); onKeyboardPress the typed one, when there is one.
local function keyEvent(name, ...)
    callEventObj({eventName = name, directEventName = "", loopable = true,
        cancellable = false, cancelled = false}, ...)
end

function __xtech_onKey(vk, down, repeated, ch)
    core.setKeyState(vk, down)
    if not repeated then
        local mapped = ((vk >= 0x30 and vk <= 0x39) or (vk >= 0x41 and vk <= 0x5A) or vk == 0x20)
            and string.char(vk) or nil
        if mapped then
            keyEvent(down and "onKeyboardKeyPress" or "onKeyboardKeyRelease", vk, mapped)
        else
            keyEvent(down and "onKeyboardKeyPress" or "onKeyboardKeyRelease", vk)
        end
    end
    if down then
        if ch ~= "" then
            keyEvent("onKeyboardPress", vk, repeated, ch)
        else
            keyEvent("onKeyboardPress", vk, repeated)
        end
    end
    return false
end

-- A frame while a script holds the game paused (Misc.pause): LunaLua's
-- runPause reads input, which sends onInputUpdate, then draws
function __xtech_onPausedFrame()
    core.updateRawKeys()
    sendEvent("onInputUpdate", false)
    return false
end

-- The engine is about to save (SaveGame, also from Misc.saveGame)
function __xtech_onSaveGame()
    sendEvent("onSaveGame", false)
    return false
end

-- exitType: the level's win type (LunaLua's GM_LEVEL_EXIT_TYPE), 0 if not won
function __xtech_onExit(exitType)
    sendEvent("onExitLevel", false, exitType or 0)
    sendEvent("onExit", false)
    if #missing > 0 then
        xtech_misc_logWarn("[smbx2core] " .. #missing .. " missing APIs this level: " .. table.concat(missing, ", "))
    end
end

if core.profileSpec then
    runCoreFile("profile.lua")
end

-- Testing: SMBX2COMPAT_CONFIG="draw-rate=2,show-frame-time=1" sets TheXTech
-- options (ini names; enums by number) for this run only, at each level's
-- start; the user's settings file is left alone
for name, value in string.gmatch(core.configSpec or "", "([%w%-_]+)=([^,]*)") do
    xtech_misc_setConfigStr(name, value)
    core.log("config: " .. name .. " = " .. value)
end

-- SMBX2COMPAT_SHOTS: frames counted from level start; the screenshots go into
-- the user folder's screenshots/
do
    local shots = {}
    for n in string.gmatch(core.shotsSpec or "", "%d+") do
        shots[tonumber(n)] = true
    end
    if next(shots) then
        local frame = 0
        core.onFrameEnd(function()
            frame = frame + 1
            if shots[frame] then
                xtech_gfx_screenshot()
            end
        end)
    end
end
