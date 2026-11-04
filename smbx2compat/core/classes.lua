--[[
  smbx2compat core: classes LunaLua implements in C++ (luabind) rather than in
  SMBX2's ffi_*.lua files: LunaLuaMain.cpp's LUAHELPER_DEF_CLASS list. Loaded by
  boot.lua after host.lua.

  Done: Animation (SMBX effects).
  Not yet: PlayerSettings, Data, Logger, RECT/RECTd, Mix_Chunk,
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
