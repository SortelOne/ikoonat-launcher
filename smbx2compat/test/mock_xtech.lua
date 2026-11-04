-- Mock of the TheXTech-Lua xtech_* API, mirroring field names from xtech_lua_bindings.cpp.
-- Raw objects use colon methods (r:getStuck()) like luabind userdata do.
local M = { log = {}, drawn = {}, sfx = {}, input = {} }

local function newLoc(x, y, w, h) return { X = x, Y = y, Width = w, Height = h, SpeedX = 0, SpeedY = 0 } end
local npcs, players = {}, {}

local function newNPC(t, x, y)
  local n = { Type = t, Killed = 0, Section = 0, Direction = -1, Hidden = false, Inert = false,
              Special = 0, Special2 = 0, Special3 = 0, Special4 = 0, Special5 = 0, TimeLeft = 100,
              Frame = 0, Projectile = false, DefaultLocationX = x, DefaultLocationY = y, DefaultType = t,
              Location = newLoc(x, y, 32, 32), _stuck = false }
  function n:getStuck() return self._stuck end
  function n:setStuck(v) self._stuck = v end
  function n:getGenerator() return false end
  function n:getPermID() for i, o in ipairs(npcs) do if o == self then return i end end end
  npcs[#npcs + 1] = n
  return n
end
M.newNPC = newNPC

players[1] = { Location = newLoc(100, 400, 24, 54), Direction = 1, Character = 1, State = 2, Mount = 0,
               MountType = 0, Section = 0, TimeToLive = 0, Effect = 0, Effect2 = 0, HoldingNPC = 0,
               StandingOnNPC = 0, Jump = 0 }

function xtech_player_get(i) return players[i] end
function xtech_player_count() return #players end
for _, k in ipairs({ "Up", "Down", "Left", "Right", "Jump", "Run", "SEL" }) do
  _G["xtech_player_pressing" .. k] = function(p) return M.input[k] == true end
end
function xtech_npc_get(i) return npcs[i] end
function xtech_npc_count() return #npcs end
function xtech_npc_create(t, x, y, sx, sy) local n = newNPC(t, x, y); n.Location.SpeedX = sx; return n end
function xtech_npc_kill(i, how)
  local n = npcs[i]; if not n then return end
  n.Killed = how
  local id = n.Type
  table.remove(npcs, i)                      -- the engine compacts the array
  if onNPCDeath then onNPCDeath(i, id, 1) end
end
function xtech_npc_hurt(i, b, c) if onNPCHurt then onNPCHurt(i, npcs[i].Type, c, b) end end
function xtech_hud_showText(s, x, y, f) M.drawn[#M.drawn + 1] = string.format("text '%s' @%d,%d f%d", s, x, y, f) end
function xtech_hud_showImage(c, x, y) M.drawn[#M.drawn + 1] = string.format("img %d @%d,%d", c, x, y) end
local images = {}
function xtech_sprite_loadImageSimple(p, code) images[code] = p end
function xtech_sprite_getImage(code)
  if not images[code] then return nil end
  return { isLoaded = function() return true end, getWidth = function() return 64 end, getHeight = function() return 32 end }
end
function xtech_audio_playSFX(id, l, v) M.sfx[#M.sfx + 1] = "sfx " .. id .. " vol " .. v end
function xtech_audio_playSFXExt(f, l, v) M.sfx[#M.sfx + 1] = "sfxfile " .. f end
function xtech_sysval_getScreenX() return 0 end
function xtech_sysval_getScreenY() return 0 end
function xtech_sysval_getScreenWidth() return 800 end
function xtech_sysval_getScreenHeight() return 600 end
local score = 0
function xtech_sysval_getScore() return score end
function xtech_sysval_setScore(v) score = v end
function xtech_sysval_getLevelBeatCode() return 9 end
function xtech_sysval_setShowInterface(v) M.hud = v end
function xtech_misc_log(s) M.log[#M.log + 1] = s end
function xtech_misc_logWarn(s) M.log[#M.log + 1] = "WARN " .. s end
function xtech_misc_showMsgInfo(s) M.log[#M.log + 1] = "MSG " .. s end
function xtech_event_triggerByName(n) M.log[#M.log + 1] = "EVENT " .. n end
-- The real fork registers luabind classes under these names
NPC, Player, Location = "luabind NPC class", "luabind Player class", "luabind Location class"
return M
