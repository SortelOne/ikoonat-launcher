-- Emulates what the patched engine does with the shim and a level's luna.lua.
package.path = "../scripts/?.lua;" .. package.path
local M = require("mock_xtech")
M.newNPC(1, 50, 400); M.newNPC(1, 300, 400); M.newNPC(27, 500, 400)

dofile("../scripts/smbx2compat.lua")               -- engine: load shim into _G
__smbx2_beginLevel("./level", "./episode")         -- engine: before scripts

local chunk = assert(loadfile("level/luna.lua"))   -- engine: sandboxed like the fork does
local env = setmetatable({}, { __index = _G })
setfenv(chunk, env); chunk()
__smbx2_attach(env)                                 -- engine: after scripts

local function frame(n)
  for i = 1, n do
    -- fork resolves hooks sandbox-first, then _G (the shim)
    local loop = rawget(env, "onLoop") or onLoop
    local hud = rawget(env, "onRenderHud") or onRenderHud
    loop(); hud(0, 1); hud(1, 1)   -- 2nd screen call must not double-draw
  end
end

frame(1)
M.input.Jump = true; frame(1); frame(1); M.input.Jump = false
xtech_player_get(1).Location.X = 100
xtech_npc_get(1).Location.X = -10   -- push a goomba offscreen: script should kill it
frame(3)
onLevelExit()

local p = xtech_player_get(1)
print("== results ==")
print("npcs left:", xtech_npc_count(), "| spawned ai1:", xtech_npc_get(xtech_npc_count()).Special)
print("goomba speedX:", xtech_npc_get(1).Location.SpeedX, "| player speedX:", p.Location.SpeedX)
print("helper ticks:", require("myhelper").ticks, "| score:", xtech_sysval_getScore(), "| SaveData.visits:", SaveData.visits)
print("sfx:", table.concat(M.sfx, ", "))
print("draw calls in last frame batch:", #M.drawn)
for i = 1, 3 do print("  ", M.drawn[i]) end
print("== log ==")
for _, l in ipairs(M.log) do print("  " .. l) end
smbx2compat.report()
print("  " .. M.log[#M.log])
