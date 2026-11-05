--[[
  smbx2compat core: the tables LunaLua's C++ registers before main.lua runs
  (CLunaLua::bindAll in LunaLuaMain.cpp). SMBX2's Lua files add to these
  rather than create them.

  Anything not defined here is logged as missing the first time it's looked up
  (core.missingAPI), and stub() marks a known function we haven't implemented:
  it logs once when called and returns nothing.
]]

local core = __smbx2core

local function hostTable(name, t)
    return setmetatable(t or {}, {
        __index = function(_, key)
            core.missingAPI(name .. "." .. tostring(key))
        end,
    })
end

local function stub(name)
    return function()
        core.missingAPI(name .. "()")
    end
end

-- Errors and debug output: LunaLua pops up a message box; we log
local function windowDebug(msg)
    core.warn(tostring(msg))
end


-------------
-- console --
-------------

-- main.lua builds print() on console:println
console = {
    print = function(self, s) core.log(tostring(s)) end,
    println = function(self, s) core.log(tostring(s)) end,
    clear = function(self) end,
}


------------
-- Native --
------------

Native = hostTable("Native", {
    getSMBXPath = getSMBXPath,
    getEpisodePath = function() return core.scriptPath(core.episodePath) end, -- ends with a separator, like LunaLua's
    simulateError = stub("Native.simulateError"),
})


----------
-- Text --
----------

Text = hostTable("Text", {
    windowDebug = windowDebug,
    windowDebugSimple = windowDebug,
    print = stub("Text.print"),
    printWP = stub("Text.printWP"),
    -- LunaLua's showSMBXMessageBox: SMBX's message box, which sends
    -- onMessageBox first (SMATRS's littleDialogue cancels it and draws its own)
    showMessageBox = function(text)
        if not core.isStarted() then
            return
        end
        text = tostring(text)
        if not core.sendEvent("onMessageBox", true, text, 1) then
            xtech_misc_showMsg(text)
        end
    end,
})


----------
-- Misc --
----------

-- LunaLua's search order: level folder, episode folder, then SMBX2's own folders
-- (LuaProxyGlobalFuncMisc.cpp, luabindResolveFile / resolveGraphicsFile)
local function search(dirs, name, wantDir)
    name = core.normalize(name)
    if string.sub(name, 1, 1) == "/" then
        dirs = { "" }
    end
    for _, dir in ipairs(dirs) do
        local path = dir .. name
        if wantDir then
            if core.isDir(path) then
                return core.scriptPath(path)
            end
        else
            local real = core.resolve(path)
            if real then
                return core.scriptPath(real)
            end
        end
    end
    return nil
end

local function searchDirs()
    return { core.customPath, core.episodePath, core.smbxPath .. "/scripts/", core.smbxPath .. "/" }
end

Misc = hostTable("Misc", {
    warning = function(msg) core.warn("Misc.warning: " .. tostring(msg)) end,
    -- The game's logic waits from the next frame on; scripts still get
    -- onInputUpdate and the drawing events (LunaLua's EventStateMachine).
    -- Pausing at frame end and pausing now both start with the next frame here.
    pause = function(atFrameEnd)
        xtech_misc_luaPause()
    end,
    unpause = function()
        xtech_misc_luaUnpause()
    end,
    isPausedByLua = function()
        return xtech_misc_isPausedByLua()
    end,

    resolveFile = function(name)
        return search(searchDirs(), name, false)
    end,
    resolveDirectory = function(name)
        return search(searchDirs(), name, true)
    end,
    resolveGraphicsFile = function(name)
        local dirs = { core.customPath, core.episodePath, core.smbxPath .. "/graphics/" }
        -- "npc-1.png" is also looked for in graphics/npc/
        if not string.find(name, "[/\\]") then
            local prefix = string.match(name, "^([^-]*)-")
            if prefix then
                dirs[#dirs + 1] = core.smbxPath .. "/graphics/" .. prefix .. "/"
            end
        end
        return search(dirs, name, false)
    end,

    listFiles = function(path) return xtech_misc_listDir(core.normalize(path), false) end,
    listDirectories = function(path) return xtech_misc_listDir(core.normalize(path), true) end,
    listLocalFiles = function(path) return xtech_misc_listDir(core.customPath .. core.normalize(path), false) end,
    listLocalDirectories = function(path) return xtech_misc_listDir(core.customPath .. core.normalize(path), true) end,
    isSamePath = function(a, b)
        local ra, rb = core.resolve(a), core.resolve(b)
        return ra ~= nil and ra == rb
    end,

    -- LunaLua keeps this from the previous level (CLunaLua::init); TODO: from the engine
    didGameOver = function() return false end,

    -- LunaLua C++ functions not done yet (bindAll, namespace Misc); each logs when called
    -- What the player has typed toward a cheat code. TODO: from the engine
    cheatBuffer = function(value)
        if value == nil then
            return ""
        end
    end,
    openPauseMenu = stub("Misc.openPauseMenu"),
    saveGame = stub("Misc.saveGame"),
    exitGame = stub("Misc.exitGame"),
    exitEngine = stub("Misc.exitEngine"),
    loadEpisode = stub("Misc.loadEpisode"),
    registerCharacterId = stub("Misc.registerCharacterId"),
    loadCharacterHitBoxes = stub("Misc.loadCharacterHitBoxes"),
    showRichDialog = stub("Misc.showRichDialog"),
    getEditorPlacedItem = stub("Misc.getEditorPlacedItem"),
    __enablePerfTracker = stub("Misc.__enablePerfTracker"),
    __disablePerfTracker = stub("Misc.__disablePerfTracker"),
    __getPerfTrackerData = stub("Misc.__getPerfTrackerData"),
    -- npcconfig_core/blockconfig_core read LunaLua's extended NPC/block
    -- properties at address + stride * id; the engine keeps them
    -- (s_memTypeArrays in xtech_lua_bindings.cpp)
    __getNPCPropertyTableAddress = function(name)
        return xtech_mem_propertyTableAddress("npc", name)
    end,
    __getBlockPropertyTableAddress = function(name)
        return xtech_mem_propertyTableAddress("block", name)
    end,
    -- SMBX's game actions, done by the engine (xtech_lua_bindings.cpp LuaMisc)
    npcToCoins = function() xtech_misc_npcToCoins() end,
    doPOW = function() xtech_misc_doPOW() end,
    doPSwitchRaw = function(activate) xtech_misc_doPSwitchRaw(activate and true or false) end,
    doPSwitch = function(activate)
        if activate == nil then
            -- no argument toggles it (LunaLua checks the switch timer)
            activate = (mem(0x00B2C62C, FIELD_WORD) == 0)
        end
        xtech_misc_doPSwitch(activate and true or false)
    end,
    doBombExplosion = function(x, y, bombType)
        xtech_misc_doBombExplosion(tonumber(x) or 0, tonumber(y) or 0, math.floor(tonumber(bombType) or 0))
    end,
    _setSemisolidCollidingFlyType = stub("Misc._setSemisolidCollidingFlyType"),
    _npcHarmCombo = function(idx, harmType, multiplier)
        return xtech_npc_harmCombo(idx, harmType, multiplier, -1)
    end,
    _npcHarmComboWithDamage = function(idx, harmType, multiplier, damage)
        return xtech_npc_harmCombo(idx, harmType, multiplier, tonumber(damage) or -1)
    end,
    _npcCollect = function(idx, playerIdx) xtech_npc_collect(idx, playerIdx) end,
    _playerHarm = function(idx) xtech_player_harm(idx) end,
    _playerKill = function(idx) xtech_player_kill(idx) end,
})


-----------
-- Audio --
-----------

-- LunaLua's Audio namespace comes from the engine (script/src/xtech_lua_audio.cpp,
-- on the same mixer LunaLua uses); here we only resolve file paths, as
-- LunaLua's getSfxPath does. Music paths may end in "|args" for the mixer.
local lunaAudio = xtech_lunaAudio

-- Resolved paths are kept: with sound off SMATRS reopens its music every frame
local audioPaths = {}

local function audioPath(path)
    path = tostring(path)
    local resolved = audioPaths[path]
    if not resolved then
        local file, args = string.match(path, "^([^|]*)(|.*)$")
        file = file or path
        local real = search(searchDirs(), file, false)
        resolved = core.normalize(real or file) .. (args or "")
        audioPaths[path] = resolved
    end
    return resolved
end

Audio = hostTable("Audio", {})
for name, fn in pairs(lunaAudio) do
    rawset(Audio, name, fn)
end

Audio.SfxOpen = function(path)
    return lunaAudio.SfxOpen(audioPath(path))
end
Audio.MusicOpen = function(path)
    lunaAudio.MusicOpen(audioPath(path))
end
Audio.playSFX = function(sound)
    if type(sound) == "number" then
        xtech_audio_playSFX(sound, 0, 128)
    else
        lunaAudio.playSFX(audioPath(sound))
    end
end
-- Section music paths stay as given (relative ones are to the level's folder)
Audio.MusicChange = function(section, music, fadeMs)
    if type(music) == "string" then
        music = core.normalize(music)
    end
    lunaAudio.MusicChange(section, music, fadeMs)
end


--------------
-- Graphics --
--------------

Graphics = hostTable("Graphics", {})


-----------
-- Level --
-----------

Level = hostTable("Level", {
    filename = function() return __xtech_levelFile end,
    name = function() return xtech_sysval_getLevelName() end,
})

-- One of LunaLua's older level globals (bindAllDeprecated); SMBX2's
-- Event:trigger() calls it too. Unknown names do nothing, as in LunaLua.
function triggerEvent(name)
    xtech_event_triggerLevelEvent(tostring(name))
end

-- Starts a section's music (SMBX's StartMusic); Player:teleport calls it
-- when the player lands in another section
function playMusic(section)
    xtech_audio_playMusic(math.floor(tonumber(section) or 0), 0)
end


-----------------
-- FileFormats --
-----------------

-- LunaLua's LuaProxyFileFormats.cpp: level, world and npc-N.txt files as
-- tables. The engine reads them (xtech_fileformats_*, same field names) and
-- leaves custom params as JSON, decoded here like LunaLua's dumpFromJson.
local function decodeParams(text)
    if text == "" or not json then
        return {}
    end
    local ok, value = pcall(json.decode, text)
    if ok and type(value) == "table" then
        return value
    end
    return {}
end

-- Lists whose items carry meta.data (sections have data directly)
local paramLists = { "blocks", "bgo", "npc", "warps", "physEnvZones", "layers", "events",
                     "terrain", "scenery", "paths", "levels", "musicBoxes" }

local function finishFile(t)
    t.data = decodeParams(t.data)
    t.meta.path = core.scriptPath(t.meta.path)
    for _, s in ipairs(t.sections or {}) do
        s.data = decodeParams(s.data)
    end
    for _, key in ipairs(paramLists) do
        local list = t[key]
        if list then
            for i = 1, #list do
                local meta = list[i].meta
                meta.data = decodeParams(meta.data)
            end
        end
    end
    return t
end

-- Relative paths are from the episode folder, as in LunaLua
local function filePath(path)
    path = core.normalize(tostring(path))
    if string.sub(path, 1, 1) ~= "/" then
        path = core.episodePath .. path
    end
    return core.resolve(path) or path
end

FileFormats = hostTable("FileFormats", {
    openLevel = function(path)
        return finishFile(xtech_fileformats_openLevel(filePath(path), false))
    end,
    openLevelHeader = function(path)
        return finishFile(xtech_fileformats_openLevel(filePath(path), true))
    end,
    -- LunaLua hands back the level it loaded; we read the same file again
    getLevelData = function()
        return finishFile(xtech_fileformats_openLevel(core.episodePath .. __xtech_levelFile, false))
    end,
    openWorld = function(path)
        return finishFile(xtech_fileformats_openWorld(filePath(path), false))
    end,
    openWorldHeader = function(path)
        return finishFile(xtech_fileformats_openWorld(filePath(path), true))
    end,
    openNpcConfig = function(path)
        return xtech_fileformats_openNpcConfig(filePath(path))
    end,
})
for name, values in pairs(xtech_fileformats_enums) do
    rawset(FileFormats, name, values)
end


--------------
-- UserData --
--------------

-- LunaLua's SaveBankProxy: numbers by name. TheXTech's LunaDLL autocode keeps
-- the same kind of variables (gAutoMan user vars); saving them with the game
-- isn't wired up yet.
UserData = hostTable("UserData", {
    setValue = function(key, value)
        xtech_var_operation(key, tonumber(value) or 0, 0) -- OP_Assign
    end,
    getValue = function(key)
        if xtech_var_exists(key) then
            return xtech_var_get(key)
        end
        return nil
    end,
    isValueSet = function(key)
        return xtech_var_exists(key)
    end,
    values = stub("UserData.values"),
    save = stub("UserData.save"),
})
