--[[
  smbx2compat core: the loading screen's Lua state (LunaLua's Misc/LoadScreen.cpp).

  The engine (xtech_lua_loadscreen.cpp) keeps a second Lua state for the
  session and draws its frames while a level loads. boot.lua sets it up as it
  does a level's (files, constants, the DLL table), then runs this instead of
  main.lua. Like LunaLua's InitMinimalLuaState we add a few globals, run SMBX2's
  scripts/base/engine/loadscreen.lua, and give the engine one function a load
  (init, which runs the episode's loadscreen.lua) and one a frame (onDraw).
]]

local core = __smbx2core
local traceback = debug.traceback
local getupvalue = debug.getupvalue -- SMBX2's lockdown hides it
local jitFlush = jit and jit.flush -- hidden later too

-- InitMinimalLuaState's globals
_smbxPath = getSMBXPath()
_episodePath = core.scriptPath(core.episodePath)
_loadScreenTimeout = 0
_loadingFinished = false
-- loadscreen.lua adds Misc.setLoadScreenTimeout and the ffi_*.lua files add
-- to it, so LunaLua's state must have the table already
Misc = Misc or {}

-- Testing: SMBX2COMPAT_LOADSHOTS=10,60 saves those frames of each loading screen
local shotFrames = {}
for n in string.gmatch(os.getenv("SMBX2COMPAT_LOADSHOTS") or "", "%d+") do
    shotFrames[tonumber(n)] = true
end
local frame = 0

do
    local path = core.smbxPath .. "/scripts/base/engine/loadscreen.lua"
    local src = core.readFile(path) or error("SMBX2 loadscreen.lua not found at " .. path)
    assert(loadstring(src, "=loadscreen.lua"))()
end

-- SMBX2's require.lua lists every global context it makes in
-- childEnvsByGlobal, a weak-keyed table whose values (the context's
-- environments) refer back to their key (env._G). LuaJIT's weak tables
-- aren't ephemerons, so no entry is ever collected. init() below makes a
-- context for the episode's loadscreen.lua at every load, and this state
-- lasts the session (as LunaLua's does), so each loading screen kept the
-- last one's modules, fonts and images (~0.5 MB a load in SMATRS). Before
-- each init, drop the contexts the earlier loading screens made.
-- LPegLJ holds them too: it keeps each pattern's values (textplus's
-- functions, from the context that made them) in valuetable until the
-- pattern is collected, and those functions keep the pattern alive. So the
-- patterns made since the state started go with them.
-- LuaJIT's compiled traces keep the tables they were compiled against (a
-- context's environments, its fonts and images) until the traces are
-- flushed, which happened only every 4-16 loads: until then each load kept
-- the last ones (~0.4 MB and 3 images each in SMATRS, 2 to 9 MB). So the
-- state's compiled code goes too; it is small and compiles again in a few
-- frames. jit.flush only touches this state's traces, not the level's.
local function upvalue(f, wanted)
    local i = 1
    while type(f) == "function" do
        local name, v = getupvalue(f, i)
        if name == nil then
            return nil
        elseif name == wanted then
            return v
        end
        i = i + 1
    end
end

local childEnvsByGlobal, bootContexts, lpegValues, bootPatterns
do
    childEnvsByGlobal = upvalue(require_utils and require_utils.makeEnvironment, "childEnvsByGlobal")
    if type(childEnvsByGlobal) == "table" then
        bootContexts = {} -- made while the state started (FFI and LPeg libraries): kept
        for k in pairs(childEnvsByGlobal) do
            bootContexts[k] = true
        end
    else
        childEnvsByGlobal = nil
        core.warn("loading screen: require.lua's childEnvsByGlobal not found; each loading screen's context will stay in memory")
    end

    lpegValues = upvalue(lpeg and lpeg.Cmt, "valuetable")
    if type(lpegValues) == "table" then
        bootPatterns = {}
        for k in pairs(lpegValues) do
            bootPatterns[k] = true
        end
    else
        lpegValues = nil
        core.warn("loading screen: LPegLJ's valuetable not found; each loading screen's patterns will stay in memory")
    end
end

local function dropOldContexts()
    if childEnvsByGlobal then
        for k in pairs(childEnvsByGlobal) do
            if not bootContexts[k] then
                childEnvsByGlobal[k] = nil
            end
        end
    end
    if lpegValues then
        for k in pairs(lpegValues) do
            if not bootPatterns[k] then
                lpegValues[k] = nil
            end
        end
    end
end

-- LunaLua shows a message box for each error; we log each once
local function call(f, ...)
    if not f then
        return
    end
    local ok, err = xpcall(f, traceback, ...)
    if not ok then
        core.warn("loading screen: " .. tostring(err))
    end
end

function __xtech_loadScreenInit()
    _loadingFinished = false
    frame = 0
    dropOldContexts()
    if jitFlush then
        jitFlush()
    end
    call(init)
end

-- finished: the level has loaded (LunaLua's killThreadFlag). Returns the
-- timeout: frames go on until that many seconds after the start.
function __xtech_loadScreenFrame(finished)
    _loadingFinished = finished
    frame = frame + 1
    if shotFrames[frame] then
        xtech_gfx_screenshot()
    end
    call(onDraw)
    return tonumber(_loadScreenTimeout) or 0
end
