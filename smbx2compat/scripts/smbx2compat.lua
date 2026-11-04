--[[
  smbx2compat.lua — SMBX2 (LunaLua) API compatibility layer for TheXTech-Lua
  ---------------------------------------------------------------------------
  Maps the most common SMBX2 script calls onto the fork's xtech_* bindings.
  Loaded once into _G by the engine (see the C++ patch). Level/episode
  luna.lua files run in sandboxes whose __index is _G, so they see these.

  Anything not implemented logs ONE warning ("[smbx2compat] not implemented:
  X") instead of crashing, so you can run a real level, read the log, and
  implement whatever shows up most. Set SMBX2COMPAT_STRICT = true to make
  missing features raise errors instead.

  Values marked VERIFY are from memory of SMBX2's API and should be checked
  against the SMBX2 documentation (docs.codehaus.moe) when testing.
]]

local compat = { version = "0.1.0" }
_G.smbx2compat = compat

-------------------------------------------------------------------------------
-- Diagnostics
-------------------------------------------------------------------------------
local warned = {}
local function log(msg)  if xtech_misc_log then xtech_misc_log("[smbx2compat] " .. msg) else print("[smbx2compat] " .. msg) end end
local function warn(msg) if xtech_misc_logWarn then xtech_misc_logWarn("[smbx2compat] " .. msg) else print("[smbx2compat] WARN " .. msg) end end

local function missing(name)
    if SMBX2COMPAT_STRICT then error("[smbx2compat] not implemented: " .. name, 3) end
    if not warned[name] then
        warned[name] = true
        warn("not implemented: " .. name)
    end
end
compat.missing = missing

-- Report everything that was hit but missing (call from a debug key, or onExit)
function compat.report()
    local list = {}
    for k in pairs(warned) do list[#list + 1] = k end
    table.sort(list)
    log(#list .. " missing feature(s) used: " .. table.concat(list, ", "))
    return list
end

-- A table that warns on access to any unknown key (used for partial libraries)
local function partial(name, t)
    return setmetatable(t, { __index = function(_, k)
        missing(name .. "." .. tostring(k))
        return function() end
    end })
end

-------------------------------------------------------------------------------
-- The fork registers luabind classes named NPC/Player/Location as globals.
-- SMBX2 needs those names, so stash the raw ones before replacing them.
-------------------------------------------------------------------------------
compat.raw = { NPC = rawget(_G, "NPC"), Player = rawget(_G, "Player") }

-------------------------------------------------------------------------------
-- Small helpers
-------------------------------------------------------------------------------
local function toSet(v)
    if v == nil or v == -1 then return nil end            -- nil / -1 = match all
    if type(v) == "number" then return { [v] = true } end
    local s = {}
    for _, x in ipairs(v) do s[x] = true end
    return s
end

-- Build an SMBX2-style wrapper object. `fields` maps SMBX2 field names to
-- either {get=fn(raw), set=fn(raw, v)} or a string naming a raw field.
local function makeClass(className, fields, methods)
    local mt = {}
    mt.__index = function(self, k)
        local m = methods[k]
        if m ~= nil then return m end
        local f = fields[k]
        if f ~= nil then
            local raw = rawget(self, "_raw")
            if type(f) == "string" then return raw[f] end
            return f.get(raw, self)
        end
        missing(className .. "." .. tostring(k))
        return nil
    end
    mt.__newindex = function(self, k, v)
        local f = fields[k]
        if f == nil then
            missing(className .. "." .. tostring(k) .. " (write)")
            return
        end
        local raw = rawget(self, "_raw")
        if type(f) == "string" then raw[f] = v
        elseif f.set then f.set(raw, v, self)
        else missing(className .. "." .. tostring(k) .. " (read-only)") end
    end
    mt.__tostring = function(self) return className .. "(" .. tostring(rawget(self, "idx")) .. ")" end
    return mt
end

local function loc(field)
    return { get = function(r) return r.Location[field] end,
             set = function(r, v) r.Location[field] = v end }
end

-------------------------------------------------------------------------------
-- Constants (VERIFY against SMBX2 docs)
-------------------------------------------------------------------------------
-- Input states. SMBX2 convention: held checks are truthy, pressed checks == KEYS_PRESSED.
KEYS_UP, KEYS_RELEASED, KEYS_PRESSED, KEYS_DOWN = false, nil, 1, true

-- Harm types; these match SMBX 1.3 KillNPC/NPCHit codes, which SMBX2 reuses.
HARM_TYPE_JUMP = 1; HARM_TYPE_FROMBELOW = 2; HARM_TYPE_NPC = 3
HARM_TYPE_PROJECTILE_USED = 4; HARM_TYPE_HELD = 5; HARM_TYPE_LAVA = 6
HARM_TYPE_TAIL = 7; HARM_TYPE_SPINJUMP = 8; HARM_TYPE_VANISH = 9; HARM_TYPE_SWORD = 10

-- Characters / powerups
CHARACTER_MARIO, CHARACTER_LUIGI, CHARACTER_PEACH, CHARACTER_TOAD, CHARACTER_LINK = 1, 2, 3, 4, 5
PLAYER_SMALL, PLAYER_BIG, PLAYER_FIREFLOWER, PLAYER_LEAF, PLAYER_TANOOKIE, PLAYER_HAMMER, PLAYER_ICE = 1, 2, 3, 4, 5, 6, 7
MOUNT_NONE, MOUNT_BOOT, MOUNT_CLOWNCAR, MOUNT_YOSHI = 0, 1, 2, 3
DIR_LEFT, DIR_RANDOM, DIR_RIGHT = -1, 0, 1

-------------------------------------------------------------------------------
-- lunatime (SMBX2 runs at ~64.1 ticks/second)
-------------------------------------------------------------------------------
local TICKS_PER_SECOND = 64.1
local tickCount, drawCount = 0, 0
lunatime = {
    tick        = function() return tickCount end,
    drawtick    = function() return drawCount end,
    time        = function() return tickCount / TICKS_PER_SECOND end,
    toSeconds   = function(t) return t / TICKS_PER_SECOND end,
    toTicks     = function(s) return math.floor(s * TICKS_PER_SECOND + 0.5) end,
}

-------------------------------------------------------------------------------
-- Player
-------------------------------------------------------------------------------
local playerCache = {}
local prevKeys = {}   -- [idx][key] = bool, for KEYS_PRESSED/RELEASED edges

local keyReaders = {
    up       = xtech_player_pressingUp,
    down     = xtech_player_pressingDown,
    left     = xtech_player_pressingLeft,
    right    = xtech_player_pressingRight,
    jump     = xtech_player_pressingJump,
    run      = xtech_player_pressingRun,
    dropItem = xtech_player_pressingSEL,
}

local function makeKeys(pw)
    return setmetatable({}, {
        __index = function(_, k)
            local reader = keyReaders[k]
            if not reader then
                if k == "altJump" or k == "altRun" or k == "pause" then
                    missing("player.keys." .. k)
                    return KEYS_UP
                end
                return nil
            end
            local now = reader(rawget(pw, "_raw")) and true or false
            local before = prevKeys[rawget(pw, "idx")] and prevKeys[rawget(pw, "idx")][k] or false
            if now and not before then return KEYS_PRESSED end
            if now then return KEYS_DOWN end
            if before then return KEYS_RELEASED end
            return KEYS_UP
        end,
        __newindex = function(_, k) missing("player.keys." .. tostring(k) .. " (write)") end,
    })
end

local playerFields = {
    x = loc("X"), y = loc("Y"), width = loc("Width"), height = loc("Height"),
    speedX = loc("SpeedX"), speedY = loc("SpeedY"),
    direction = "Direction", character = "Character", powerup = "State",
    mount = "Mount", mountType = "MountType", section = "Section",
    deathTimer = "TimeToLive", forcedState = "Effect", forcedTimer = "Effect2",
    isValid = { get = function(r, self) return self.idx <= xtech_player_count() end },
    holdingNPC = { get = function(r) return r.HoldingNPC > 0 and NPC(r.HoldingNPC) or nil end },
    keys = { get = function(r, self) return rawget(self, "_keys") end },
}
local playerMethods = {
    isOnGround = function(self)
        -- Approximation: standing on an NPC or not moving vertically while not jumping. VERIFY in play.
        local r = self._raw
        return r.StandingOnNPC ~= 0 or (r.Location.SpeedY == 0 and r.Jump == 0)
    end,
    mem  = function() missing("Player:mem") return 0 end,
    harm = function() missing("Player:harm") end,
    kill = function() missing("Player:kill") end,
    teleport = function(self, x, y, bottomCenter)
        local L = self._raw.Location
        if bottomCenter then L.X = x - L.Width / 2; L.Y = y - L.Height
        else L.X = x; L.Y = y end
    end,
}
local playerMT = makeClass("Player", playerFields, playerMethods)

local function getPlayer(idx)
    local raw = xtech_player_get(idx)
    if not raw then return nil end
    local pw = playerCache[idx]
    if not pw or rawget(pw, "_raw") ~= raw then
        pw = setmetatable({ _raw = raw, idx = idx }, playerMT)
        rawset(pw, "_keys", makeKeys(pw))
        playerCache[idx] = pw
    end
    return pw
end

Player = setmetatable({
    get   = function()
        local t = {}
        for i = 1, xtech_player_count() do t[#t + 1] = getPlayer(i) end
        return t
    end,
    count = function() return xtech_player_count() end,
    getTemplates = function() missing("Player.getTemplates") return {} end,
}, { __call = function(_, idx) return getPlayer(idx or 1) end })

local function snapshotKeys()
    for i = 1, xtech_player_count() do
        local raw = xtech_player_get(i)
        local k = prevKeys[i] or {}
        for name, reader in pairs(keyReaders) do k[name] = reader(raw) and true or false end
        prevKeys[i] = k
    end
end

-------------------------------------------------------------------------------
-- NPC
-------------------------------------------------------------------------------
-- NOTE: like SMBX itself, TheXTech compacts the NPC array when NPCs die, so a
-- wrapper's index can come to point at a different NPC. SMBX2 has the same
-- caveat (it is why scripts check isValid every tick). npc.data is keyed by
-- the raw object and is cleared when that object's type changes.
local npcCache, npcData = {}, setmetatable({}, { __mode = "k" })

local npcFields = {
    x = loc("X"), y = loc("Y"), width = loc("Width"), height = loc("Height"),
    speedX = loc("SpeedX"), speedY = loc("SpeedY"),
    id = "Type", direction = "Direction", section = "Section",
    ai1 = "Special", ai2 = "Special2", ai3 = "Special3", ai4 = "Special4", ai5 = "Special5",
    isHidden = "Hidden", friendly = "Inert", killFlag = "Killed",
    despawnTimer = "TimeLeft", animationFrame = "Frame", isProjectile = "Projectile",
    spawnX = "DefaultLocationX", spawnY = "DefaultLocationY", spawnId = "DefaultType",
    dontMove = { get = function(r) return r:getStuck() end, set = function(r, v) r:setStuck(v and true or false) end },
    isValid = { get = function(r, self) return self.idx >= 1 and self.idx <= xtech_npc_count() and r.Killed == 0 end },
    isGenerator = { get = function(r) return r:getGenerator() end },
    data = { get = function(r)
        local d = npcData[r]
        if not d or d._type ~= r.Type then d = { _type = r.Type }; npcData[r] = d end
        return d
    end },
}
local npcMethods = {
    kill = function(self, harmType) xtech_npc_kill(self.idx, harmType or HARM_TYPE_VANISH) end,
    harm = function(self, harmType) xtech_npc_hurt(self.idx, harmType or HARM_TYPE_NPC, 0) end,
    mem  = function() missing("NPC:mem") return 0 end,
    transform = function(self, newId) self._raw.Type = newId; missing("NPC:transform (only id changed; size/config not refreshed)") end,
    toCoin    = function() missing("NPC:toCoin") end,
    attachWeights = function() missing("NPC:attachWeights") end,
}
local npcMT = makeClass("NPC", npcFields, npcMethods)

local function wrapNPC(idx, raw)
    raw = raw or xtech_npc_get(idx)
    if not raw then return nil end
    local w = npcCache[idx]
    if not w or rawget(w, "_raw") ~= raw then
        w = setmetatable({ _raw = raw, idx = idx }, npcMT)
        npcCache[idx] = w
    end
    return w
end

NPC = setmetatable({
    count = function() return xtech_npc_count() end,

    -- NPC.get(ids, sections): ids/sections may be nil, -1, a number or a list.
    get = function(ids, sections)
        local idSet, secSet = toSet(ids), toSet(sections)
        local out = {}
        for i = 1, xtech_npc_count() do
            local raw = xtech_npc_get(i)
            if raw and raw.Killed == 0
               and (not idSet or idSet[raw.Type])
               and (not secSet or secSet[raw.Section]) then
                out[#out + 1] = wrapNPC(i, raw)
            end
        end
        return out
    end,

    iterate = function(ids, sections)
        local list, i = NPC.get(ids, sections), 0
        return function() i = i + 1; local v = list[i]; if v then return v.idx, v end end
    end,

    -- NPC.spawn(id, x, y, section, respawn, centered)
    spawn = function(id, x, y, section, respawn, centered)
        local raw = xtech_npc_create(id, x, y, 0, 0)
        if not raw then warn("NPC.spawn failed (NPC limit?) for id " .. tostring(id)); return nil end
        if section then raw.Section = section end
        if centered then
            raw.Location.X = raw.Location.X - raw.Location.Width / 2
            raw.Location.Y = raw.Location.Y - raw.Location.Height / 2
        end
        if respawn then missing("NPC.spawn respawn flag") end
        return wrapNPC(raw:getPermID(), raw)
    end,

    getIntersecting = function(x1, y1, x2, y2)
        local out = {}
        for i = 1, xtech_npc_count() do
            local raw = xtech_npc_get(i)
            if raw and raw.Killed == 0 then
                local L = raw.Location
                if L.X < x2 and L.X + L.Width > x1 and L.Y < y2 and L.Y + L.Height > y1 then
                    out[#out + 1] = wrapNPC(i, raw)
                end
            end
        end
        return out
    end,

    config = setmetatable({}, { __index = function(_, id)
        missing("NPC.config")
        return {}
    end }),
}, { __call = function(_, idx) return wrapNPC(idx) end })

-------------------------------------------------------------------------------
-- Camera
-------------------------------------------------------------------------------
local function makeCamera(screen)
    return setmetatable({}, {
        __index = function(_, k)
            if k == "x" then return xtech_sysval_getScreenX(screen)
            elseif k == "y" then return xtech_sysval_getScreenY(screen)
            elseif k == "width" then return xtech_sysval_getScreenWidth(screen)
            elseif k == "height" then return xtech_sysval_getScreenHeight(screen)
            elseif k == "idx" then return screen end
            missing("camera." .. tostring(k))
        end,
        __newindex = function(_, k) missing("camera." .. tostring(k) .. " (write)") end,
    })
end
camera, camera2 = makeCamera(1), makeCamera(2)
Camera = { get = function() return { camera, camera2 } end }

-------------------------------------------------------------------------------
-- Text / Graphics  (only valid inside onDraw, like SMBX2)
-------------------------------------------------------------------------------
local function fontFor(t)
    -- SMBX2 Text types 1-4 -> fork fonts 1-5 (VERIFY visually; 3 is the usual default)
    if t == nil then return 3 end
    return math.max(1, math.min(5, t))
end

Text = partial("Text", {
    -- Text.print(text, x, y) or Text.print(text, type, x, y)
    print = function(text, a, b, c)
        if c ~= nil then xtech_hud_showText(tostring(text), b, c, fontFor(a))
        else xtech_hud_showText(tostring(text), a, b, 3) end
    end,
    printWP = function(text, a, b, c, d)
        -- priority argument is ignored (no render priority in the fork)
        if d ~= nil then xtech_hud_showText(tostring(text), b, c, fontFor(a))
        else xtech_hud_showText(tostring(text), a, b, 3) end
    end,
    windowDebug = function(s) xtech_misc_showMsgInfo(tostring(s)) end,
    showMessageBox = function(s) xtech_misc_showMsg(tostring(s)) end,
})

local nextImageCode = 20000
local function loadImage(path)
    local code = nextImageCode
    nextImageCode = nextImageCode + 1
    xtech_sprite_loadImageSimple(path, code)
    local img = xtech_sprite_getImage(code)
    if not img or not img:isLoaded() then
        warn("Graphics.loadImage failed: " .. tostring(path))
        return nil
    end
    return { _code = code, width = img:getWidth(), height = img:getHeight(), path = path }
end

local function drawImage(img, x, y, sx, sy, w, h, opacity)
    if not img then return end
    -- SMBX2 overload: drawImage(img, x, y, opacity)
    if sx ~= nil and sy == nil and sx <= 1 then opacity, sx = sx, nil end
    if opacity and opacity < 1 then missing("Graphics.drawImage opacity") end
    xtech_hud_showImage(img._code, x, y, sx or 0, sy or 0, w or 0, h or 0)
end

Graphics = partial("Graphics", {
    loadImage = loadImage,
    loadImageResolved = loadImage,
    drawImage = drawImage,
    drawImageWP = function(img, x, y, ...)
        local args = { ... }
        table.remove(args) -- last argument is priority; ignored
        drawImage(img, x, y, unpack(args))
    end,
    drawImageToScene = function(img, x, y, ...)
        drawImage(img, x - camera.x, y - camera.y, ...)
    end,
    drawImageToSceneWP = function(img, x, y, ...)
        local args = { ... }
        table.remove(args)
        drawImage(img, x - camera.x, y - camera.y, unpack(args))
    end,
    -- Graphics.draw{type=RTYPE_IMAGE|RTYPE_TEXT, image=, text=, x=, y=, sceneCoords=, sourceX=...}
    draw = function(args)
        local x, y = args.x or 0, args.y or 0
        if args.sceneCoords or args.isSceneCoordinates then x, y = x - camera.x, y - camera.y end
        if args.type == RTYPE_TEXT then
            xtech_hud_showText(tostring(args.text), x, y, fontFor(args.fontType))
        else
            drawImage(args.image, x, y, args.sourceX, args.sourceY, args.sourceWidth, args.sourceHeight, args.opacity)
        end
    end,
    activateHud = function(on) xtech_sysval_setShowInterface(on and true or false) end,
    isHudActivated = function() return xtech_sysval_getShowInterface() end,
})
RTYPE_IMAGE, RTYPE_TEXT = 1, 2

-------------------------------------------------------------------------------
-- Audio
-------------------------------------------------------------------------------
SFX = partial("SFX", {
    -- SFX.play(idOrFile, volume 0-1, loops)
    play = function(s, volume, loops)
        local vol = math.floor((volume or 1) * 128 + 0.5)
        local lp = (loops or 1) - 1
        if type(s) == "number" then xtech_audio_playSFX(s, lp, vol)
        elseif type(s) == "string" then xtech_audio_playSFXExt(s, lp, vol)
        else missing("SFX.play with " .. type(s)) end
    end,
    open = function(path) xtech_audio_preloadSFXExt(path); return path end,
})

Audio = partial("Audio", {
    MusicChange = function(section, m)
        if type(m) == "string" then xtech_section_setMusicFile(section, m)
        else xtech_section_setMusic(section, m) end
        xtech_audio_playMusic(section, 0)
    end,
    MusicStop   = function() xtech_audio_stopMusic() end,
    MusicPause  = function() xtech_audio_pauseMusic() end,
    MusicResume = function() xtech_audio_resumeMusic() end,
    MusicFadeOut = function(_, ms) xtech_audio_fadeOutMusic(ms or 1000) end,
    playSFX = function(s) SFX.play(s) end,
})

-------------------------------------------------------------------------------
-- Section, Layer, events, Misc
-------------------------------------------------------------------------------
Section = setmetatable({
    count = function() return xtech_section_count() end,
}, { __call = function(_, idx)
    return setmetatable({ idx = idx }, { __index = function(_, k)
        if k == "boundary" then
            local s = xtech_section_get(idx)
            return { left = s.X, top = s.Y, right = s.X + s.Width, bottom = s.Y + s.Height }
        elseif k == "backgroundID" then return xtech_section_getBackground(idx)
        elseif k == "musicID" then return xtech_section_getMusic(idx) end
        missing("Section." .. tostring(k))
    end, __newindex = function(_, k, v)
        if k == "backgroundID" then xtech_section_setBackground(idx, v)
        elseif k == "musicID" then xtech_section_setMusic(idx, v)
        else missing("Section." .. tostring(k) .. " (write)") end
    end })
end })

local function findLayer(name)
    for i = 0, xtech_layer_count() do
        local l = xtech_layer_get(i)
        if l and l.Name == name then return l end
    end
end
Layer = partial("Layer", {
    get = function(name)
        local raw = findLayer(name)
        if not raw then return nil end
        return setmetatable({ name = name }, { __index = function(self, k)
            if k == "speedX" then return raw.SpeedX
            elseif k == "speedY" then return raw.SpeedY
            elseif k == "isHidden" then return raw.Hidden
            elseif k == "stop" then return function() xtech_layer_stop(raw) end
            elseif k == "show" or k == "hide" or k == "toggle" then
                -- Hiding a layer must also hide its objects; only the event system does that properly.
                return function() missing("Layer:" .. k .. " (use triggerEvent with a level event instead)") end
            end
            missing("Layer." .. tostring(k))
        end, __newindex = function(_, k, v)
            if k == "speedX" then xtech_layer_setXSpeed(raw, v)
            elseif k == "speedY" then xtech_layer_setYSpeed(raw, v)
            else missing("Layer." .. tostring(k) .. " (write)") end
        end })
    end,
})

function triggerEvent(name) xtech_event_triggerByName(name) end

Misc = partial("Misc", {
    dialog = function(...)
        local parts = {}
        for i = 1, select("#", ...) do parts[#parts + 1] = tostring(select(i, ...)) end
        xtech_misc_showMsgInfo(table.concat(parts, "\n"))
    end,
    score = function(n) if n then xtech_sysval_setScore(xtech_sysval_getScore() + n) end return xtech_sysval_getScore() end,
    coins = function(n) if n then xtech_sysval_setCoins(xtech_sysval_getCoins() + n) end return xtech_sysval_getCoins() end,
    cheatBuffer = function() missing("Misc.cheatBuffer") return "" end,
    resolveFile = function(p) return p end,  -- the fork resolves paths itself
    inEditor = function() return false end,
})

function mem() missing("mem()") return 0 end

-------------------------------------------------------------------------------
-- Saved / transient data
-------------------------------------------------------------------------------
GameData = rawget(_G, "GameData") or {}   -- survives level changes (VM is persistent)
SaveData = rawget(_G, "SaveData") or {}   -- written to the save file below

-- Only install save hooks if the episode's game.lua didn't provide its own.
if rawget(_G, "OnGameSave") == nil then
    function OnGameSave() return { smbx2 = SaveData, custom = rawget(_G, "CustomData") } end
end
if rawget(_G, "OnGameLoad") == nil then
    function OnGameLoad(data)
        data = data or {}
        SaveData = data.smbx2 or {}
        if data.custom then CustomData = data.custom end
    end
end

-------------------------------------------------------------------------------
-- Math / table extras that SMBX2 adds to the standard library
-------------------------------------------------------------------------------
math.clamp = math.clamp or function(v, lo, hi) lo, hi = lo or 0, hi or 1; return math.max(lo, math.min(hi, v)) end
math.lerp  = math.lerp  or function(a, b, t) return a + (b - a) * t end
math.sign  = math.sign  or function(v) return v > 0 and 1 or (v < 0 and -1 or 0) end
table.ifind = table.ifind or function(t, v) for i, x in ipairs(t) do if x == v then return i end end end
table.contains = table.contains or function(t, v) return table.ifind(t, v) ~= nil end

-------------------------------------------------------------------------------
-- Routine (coroutines driven by onTick)
-------------------------------------------------------------------------------
local routines = {}
Routine = {}
function Routine.run(fn, ...)
    local co = coroutine.create(fn)
    local r = { co = co, wait = 0, args = { ... }, started = false }
    routines[#routines + 1] = r
    return r
end
function Routine.waitFrames(n) coroutine.yield(math.max(1, n or 1)) end
function Routine.wait(seconds) coroutine.yield(math.max(1, lunatime.toTicks(seconds or 0))) end
function Routine.skip() coroutine.yield(1) end
function Routine.abort(r) if r then r.dead = true end end
function Routine.setFrameTimer(frames, fn, repeats)
    return Routine.run(function()
        local n = 0
        repeat
            Routine.waitFrames(frames); fn(); n = n + 1
        until repeats ~= true and n >= (tonumber(repeats) or 1)
    end)
end
function Routine.setTimer(seconds, fn, repeats) return Routine.setFrameTimer(lunatime.toTicks(seconds), fn, repeats) end

local function updateRoutines()
    local i = 1
    while i <= #routines do
        local r = routines[i]
        if not r.dead then
            if r.wait > 0 then r.wait = r.wait - 1 end
            if r.wait == 0 then
                local ok, res
                if not r.started then r.started = true; ok, res = coroutine.resume(r.co, unpack(r.args))
                else ok, res = coroutine.resume(r.co) end
                if not ok then warn("Routine error: " .. tostring(res)); r.dead = true
                elseif coroutine.status(r.co) == "dead" then r.dead = true
                else r.wait = (tonumber(res) or 1) - 1 end
            end
        end
        if r.dead then table.remove(routines, i) else i = i + 1 end
    end
end

-------------------------------------------------------------------------------
-- Event system: registerEvent + dispatch from the fork's hooks
-------------------------------------------------------------------------------
-- handlers[eventName] = { before = {...}, after = {...} } of {obj, fn}
local handlers = {}
local envs = {}           -- script sandboxes: episode first, then level
local started = false

function registerEvent(obj, eventName, funcName, beforeMainCall)
    local h = handlers[eventName]
    if not h then h = { before = {}, after = {} }; handlers[eventName] = h end
    local list = beforeMainCall and h.before or h.after
    list[#list + 1] = { obj = obj, name = funcName or eventName }
end

function unregisterEvent(obj, eventName, funcName)
    local h = handlers[eventName]
    if not h then return end
    funcName = funcName or eventName
    for _, list in ipairs({ h.before, h.after }) do
        for i = #list, 1, -1 do
            if list[i].obj == obj and list[i].name == funcName then table.remove(list, i) end
        end
    end
end

local SUPPORTED_EVENTS = {
    onStart = 1, onTick = 1, onTickEnd = 1, onDraw = 1, onDrawEnd = 1, onExit = 1, onExitLevel = 1,
    onPostNPCKill = 1, onNPCKill = 1, onPostNPCHarm = 1, onNPCHarm = 1,
    onPostPlayerHarm = 1, onPlayerHarm = 1, onPostPlayerKill = 1, onPlayerKill = 1,
    onPause = 1, onUnpause = 1, onPostBlockHit = 1, onBlockHit = 1,
    onInitAPI = 1, onLoad = 1,
}

local function callSafe(fn, name, ...)
    local ok, err = pcall(fn, ...)
    if not ok then warn("error in " .. name .. ": " .. tostring(err)) end
end

local function dispatch(eventName, ...)
    local h = handlers[eventName]
    if h then
        for _, e in ipairs(h.before) do
            local fn = e.obj[e.name]
            if fn then callSafe(fn, eventName, ...) end
        end
    end
    -- main scripts: global functions defined in episode/level luna.lua
    for _, env in ipairs(envs) do
        local fn = rawget(env, eventName)
        if type(fn) == "function" then callSafe(fn, eventName, ...) end
    end
    if h then
        for _, e in ipairs(h.after) do
            local fn = e.obj[e.name]
            if fn then callSafe(fn, eventName, ...) end
        end
    end
end
compat.dispatch = dispatch

-- Cancellable SMBX2 events: the fork only fires after the fact, so cancel is ignored.
local function eventObj(name)
    return setmetatable({ cancelled = false }, { __newindex = function(t, k, v)
        if k == "cancelled" and v then missing(name .. " cancellation (fork events fire after the fact)") end
        rawset(t, k, v)
    end })
end

-- Called by the engine BEFORE running luna.lua files (sets require paths).
function __smbx2_beginLevel(levelDir, episodeDir)
    envs, handlers, routines, started = {}, {}, {}, false
    npcCache, playerCache, prevKeys = {}, {}, {}
    tickCount, drawCount = 0, 0
    local paths = {}
    local function add(dir)
        if dir and dir ~= "" then
            if dir:sub(-1) ~= "/" then dir = dir .. "/" end
            paths[#paths + 1] = dir .. "?.lua"
            paths[#paths + 1] = dir .. "scripts/?.lua"
        end
    end
    add(levelDir); add(episodeDir)
    compat.basePath = compat.basePath or package.path
    package.path = table.concat(paths, ";") .. ";" .. compat.basePath
end

-- Called by the engine AFTER running scripts, with their sandboxes (episode, level).
function __smbx2_attach(...)
    for i = 1, select("#", ...) do
        local env = select(i, ...)
        if type(env) == "table" then envs[#envs + 1] = env end
    end
    for _, env in ipairs(envs) do
        for _, legacy in ipairs({ "onEvent", "onKeyDown", "onKeyUp", "onInputUpdate", "onCameraUpdate",
                                  "onNPCCollect", "onPostNPCCollect", "onMessageBox", "onWarpEnter" }) do
            if type(rawget(env, legacy)) == "function" then missing("event " .. legacy) end
        end
    end
end

-- Unknown require()s get a warning stub instead of killing the whole script,
-- so a level can limp along and you can see what to port next.
table.insert(package.loaders, function(name)
    return function()
        missing("require('" .. name .. "')")
        if SMBX2COMPAT_STRICT then error("module not found: " .. name) end
        return partial("require('" .. name .. "')", {})
    end
end)

-------------------------------------------------------------------------------
-- Fork hooks (_G fallbacks; the fork looks in the level sandbox first, then _G)
-------------------------------------------------------------------------------
function onLoop()
    if not started then
        started = true
        dispatch("onStart")
    end
    tickCount = tickCount + 1
    updateRoutines()
    dispatch("onTick")
    dispatch("onTickEnd")
    snapshotKeys()           -- after scripts ran, so next tick can detect edges
end

local lastDrawTick = -1
function onRenderHud(Z, numScreens)
    -- The fork calls this once per screen; SMBX2's onDraw runs once per frame.
    if lastDrawTick == tickCount then return end
    lastDrawTick = tickCount
    drawCount = drawCount + 1
    dispatch("onDraw")
    dispatch("onDrawEnd")
end

function onLevelExit()
    dispatch("onExitLevel", xtech_sysval_getLevelBeatCode())
    dispatch("onExit")
end

function onNPCDeath(permid, npcId, killer)
    local w = wrapNPC(permid) or { idx = permid, id = npcId }
    dispatch("onNPCKill", eventObj("onNPCKill"), w, HARM_TYPE_NPC)
    dispatch("onPostNPCKill", w, HARM_TYPE_NPC)  -- real harm type not provided by the fork
end

function onNPCHurt(permid, npcId, hitterId, hitType)
    local w = wrapNPC(permid)
    dispatch("onNPCHarm", eventObj("onNPCHarm"), w, hitType, nil)
    dispatch("onPostNPCHarm", w, hitType, nil)
end

function onPlayerHurt(playerId, causeNpcId)
    local p = getPlayer(playerId)
    dispatch("onPlayerHarm", eventObj("onPlayerHarm"), p)
    dispatch("onPostPlayerHarm", p)
end

function onPlayerDeath(playerId, causeNpcId)
    local p = getPlayer(playerId)
    dispatch("onPlayerKill", eventObj("onPlayerKill"), p)
    dispatch("onPostPlayerKill", p)
end

function onBlockHit(blockId, blockType, hitterId, hitStyle)
    dispatch("onBlockHit", eventObj("onBlockHit"), { idx = blockId, id = blockType }, hitterId < 0, nil)
    dispatch("onPostBlockHit", { idx = blockId, id = blockType }, hitterId < 0, nil)
end

function onPause()   dispatch("onPause", eventObj("onPause"), Player(1)) end
function onUnpause() dispatch("onUnpause") end

-- Convenience globals
setmetatable(_G, nil) -- (no-op; _G has no metatable in the fork, kept explicit)
compat.refreshPlayerGlobals = function() player = Player(1); player2 = Player(2) end
local _onLoop = onLoop
function onLoop() compat.refreshPlayerGlobals(); _onLoop() end

log("loaded v" .. compat.version)
return compat
