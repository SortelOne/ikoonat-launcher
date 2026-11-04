-- Written the way a typical SMBX2 level script is written
local helper = require("myhelper")
local npcutils = require("npcs/npcutils")     -- basegame lib: not ported, should warn not crash

local coinImg = Graphics.loadImage("coin.png")
local stompCount = 0

function onStart()
  SaveData.visits = (SaveData.visits or 0) + 1
  for _, v in ipairs(NPC.get(1)) do v.speedX = -2; v.data.hp = 3 end
  local s = NPC.spawn(89, 400, 300, player.section, false, true)
  s.ai1 = 7
  Routine.run(function()
    Routine.waitFrames(3)
    triggerEvent("bossAppear")
    SFX.play(43)
  end)
end

function onTick()
  if player.keys.jump == KEYS_PRESSED then SFX.play("custom.ogg", 0.5) end
  for _, v in ipairs(NPC.get({1, 89})) do
    if v.isValid and v.x < 0 then v:kill(HARM_TYPE_VANISH) end
  end
  player.speedX = math.clamp(player.speedX + 0.5, -6, 6)
end

function onDraw()
  Text.print("Stomps: " .. stompCount, 3, 600, 20)
  Graphics.drawImage(coinImg, 700, 20)
end

function onPostNPCKill(npc, harmType)
  stompCount = stompCount + 1
  Misc.score(100)
end

function onEvent(name) end   -- unsupported legacy event: should warn once
