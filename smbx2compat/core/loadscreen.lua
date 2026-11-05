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
