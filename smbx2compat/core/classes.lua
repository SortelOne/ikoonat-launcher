--[[
  smbx2compat core: classes LunaLua implements in C++ (luabind) rather than in
  SMBX2's ffi_*.lua files: LunaLuaMain.cpp's LUAHELPER_DEF_CLASS list. Loaded by
  boot.lua after host.lua.

  Done: Animation (SMBX effects), PlayerSettings.
  Not yet: Data, Logger, RECT/RECTd, Mix_Chunk,
  PlayingSfxInstance, NativeInputConfig; the world-map classes (World, Tile,
  Scenery, Path, Musicbox, Level objects) come with Phase 6.
]]

local core = __smbx2core


---------------
-- Animation --
---------------
-- LunaLua's Animation is an SMBX effect, indexed from 0. The engine's effects
-- are 1-based (xtech_effect_get).

local effectGet = xtech_effect_get
local effectCount = xtech_effect_count

local Animation = {}
local AnimationMT = {}

local function animationAt(idx)
    return setmetatable({ idx = idx }, AnimationMT)
end

local fields = {
    id = function(e) return e.Type end,
    x = function(e) return e.Location.X end,
    y = function(e) return e.Location.Y end,
    speedX = function(e) return e.Location.SpeedX end,
    speedY = function(e) return e.Location.SpeedY end,
    width = function(e) return e.Location.Width end,
    height = function(e) return e.Location.Height end,
    timer = function(e) return e.Life end,
    animationFrame = function(e) return e.Frame end,
    subTimer = function(e) return e.FrameCount end,
    npcID = function(e) return e.NewNpc end,
}

AnimationMT.__type = "Animation"
AnimationMT.__index = function(self, key)
    if key == "isValid" then
        return effectGet(self.idx + 1) ~= nil
    end
    local get = fields[key]
    if get then
        local e = effectGet(self.idx + 1)
        return e and get(e)
    end
    local method = Animation[key]
    if method == nil then
        core.missingAPI("Animation." .. tostring(key))
    end
    return method
end
AnimationMT.__eq = function(a, b)
    return a.idx == b.idx
end

function Animation.count()
    return effectCount()
end

local function matchesIds(id, ids)
    if ids == nil or ids == -1 then
        return true
    end
    if type(ids) == "number" then
        return id == ids
    end
    for _, v in ipairs(ids) do
        if v == id then
            return true
        end
    end
    return false
end

function Animation.get(ids)
    local ret = {}
    for i = 1, effectCount() do
        local e = effectGet(i)
        if e and matchesIds(e.Type, ids) then
            ret[#ret + 1] = animationAt(i - 1)
        end
    end
    return ret
end

function Animation.getIntersecting(x1, y1, x2, y2)
    local ret = {}
    for i = 1, effectCount() do
        local e = effectGet(i)
        if e then
            local l = e.Location
            if l.X <= x2 and l.X + l.Width >= x1 and l.Y <= y2 and l.Y + l.Height >= y1 then
                ret[#ret + 1] = animationAt(i - 1)
            end
        end
    end
    return ret
end

-- Animation.spawn(id, x, y[, animationFrame])
function Animation.spawn(id, x, y, frame)
    local e = xtech_effect_create(id, x, y, 1, false)
    if not e then
        return nil
    end
    if frame then
        e.Frame = frame
    end
    return animationAt(effectCount() - 1)
end

_G.Animation = setmetatable(Animation, {
    __call = function(_, idx)
        return animationAt(idx)
    end,
})


--------------------
-- PlayerSettings --
--------------------
-- Hitbox, grab spot and sprite offsets per character and powerup
-- (LuaProxyPlayerSettings.cpp); the engine keeps them in Physics and
-- PlayerFrameX/Y. LunaLua raises an error for a bad character or powerup.

local settingsGet = xtech_playerSettings_get
local settingsSet = xtech_playerSettings_set
local NUM_CHARACTERS = 50 -- numCharacters in global_constants.h
local NUM_STATES = 50     -- numStates

local PlayerSettings = {}
local PlayerSettingsMT = { __type = "PlayerSettings" }

local settingsFields = {
    hitboxWidth = true,
    hitboxHeight = true,
    hitboxDuckHeight = true,
    grabOffsetX = true,
    grabOffsetY = true,
}

local function checkSettings(character, powerup)
    if type(character) ~= "number" or character < 1 or character > NUM_CHARACTERS then
        error("Invalid character id " .. tostring(character) .. "!", 3)
    end
    if type(powerup) ~= "number" or powerup < 1 or powerup > NUM_STATES then
        error("Invalid powerup id " .. tostring(powerup) .. "!", 3)
    end
end

local function spriteOffset(self, y, indexX, indexY, value)
    checkSettings(self.character, self.powerup)
    local ok
    if value == nil then
        ok = xtech_playerSettings_getSpriteOffset(self.character, self.powerup, y, indexX, indexY)
    else
        ok = xtech_playerSettings_setSpriteOffset(self.character, self.powerup, y, indexX, indexY, value)
    end
    if not ok then
        error(string.format("indexX (%d) with indexY (%d) are invalid coordinate indexes", indexX, indexY), 3)
    end
    return ok
end

local methods = {
    getSpriteOffsetX = function(self, x, y) return spriteOffset(self, false, x, y) end,
    getSpriteOffsetY = function(self, x, y) return spriteOffset(self, true, x, y) end,
    setSpriteOffsetX = function(self, x, y, v) spriteOffset(self, false, x, y, v) end,
    setSpriteOffsetY = function(self, x, y, v) spriteOffset(self, true, x, y, v) end,
}

PlayerSettingsMT.__index = function(self, key)
    if settingsFields[key] then
        checkSettings(rawget(self, "character"), rawget(self, "powerup"))
        return settingsGet(rawget(self, "character"), rawget(self, "powerup"), key)
    end
    local method = methods[key]
    if method == nil then
        core.missingAPI("PlayerSettings." .. tostring(key))
    end
    return method
end

PlayerSettingsMT.__newindex = function(self, key, value)
    if settingsFields[key] then
        checkSettings(rawget(self, "character"), rawget(self, "powerup"))
        settingsSet(rawget(self, "character"), rawget(self, "powerup"), key, value)
    elseif key == "character" or key == "powerup" then
        rawset(self, key, value)
    else
        error("Cannot set PlayerSettings." .. tostring(key), 2)
    end
end

function PlayerSettings.get(character, powerup)
    checkSettings(character, powerup)
    return setmetatable({ character = character, powerup = powerup }, PlayerSettingsMT)
end

_G.PlayerSettings = PlayerSettings


-----------------
-- InputConfig --
-----------------

-- LunaLua's inputConfig1/inputConfig2 (LuaProxyNativeInput.cpp): the input
-- type (0 = keyboard) and each key as a Windows key code. TheXTech's control
-- profiles use SDL codes, so these hold SMBX's default keys for now; changes
-- are kept but don't reach the engine. TODO: map from TheXTech's profiles.
local function inputConfig(idx)
    return {
        idx = idx,
        inputType = 0,
        up = VK_UP, down = VK_DOWN, left = VK_LEFT, right = VK_RIGHT,
        run = 0x58, altrun = 0x53,    -- X, S
        jump = 0x5A, altjump = 0x41,  -- Z, A
        dropitem = VK_SHIFT, pause = VK_ESCAPE,
    }
end

_G.inputConfig1 = inputConfig(1)
_G.inputConfig2 = inputConfig(2)


----------------
-- Legacy NPC --
----------------

-- ffi_npc.lua keeps the NPC class LunaLua registers in C++ (as LegacyNPC) and
-- builds its NPC.spawn on LegacyNPC.spawn; nothing else of that class is used.
-- Before this, NPC is still the engine's own class, which has no spawn.
local NPC_MAX_ID = 1000 -- LunaLua's NPC::MAX_ID (the engine's 1001+ are TheXTech's own)

_G.NPC = {
    spawn = function(id, x, y, section, respawn, centered)
        id, section = math.floor(tonumber(id) or 0), math.floor(tonumber(section) or -1)
        if id < 1 or id > NPC_MAX_ID then
            error("Invalid NPC-ID!\nNeed NPC-ID between 1-" .. NPC_MAX_ID .. "\nGot NPC-ID: " .. tostring(id), 2)
        end
        if section < 0 or section > 20 then
            error("Invalid Section!\nNeed Section-Index between 0-20\nGot Section-Index: " .. tostring(section), 2)
        end
        local idx = xtech_npc_spawnLegacy(id, tonumber(x) or 0, tonumber(y) or 0, section,
                                          respawn and true or false, centered and true or false)
        if idx < 0 then
            error("Over 5000 NPCs, cannot spawn more!", 2)
        end
        return { idx = idx }
    end,
}
