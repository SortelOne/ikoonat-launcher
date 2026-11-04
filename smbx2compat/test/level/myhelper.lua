-- An SMBX2-style library: a table + registerEvent, loaded with require()
local helper = {}
helper.ticks = 0
function helper.onInitAPI()
  registerEvent(helper, "onTick")
  registerEvent(helper, "onDraw", "drawStuff")
end
function helper.onTick() helper.ticks = helper.ticks + 1 end
function helper.drawStuff() Text.print("helper ticks: " .. helper.ticks, 10, 10) end
helper.onInitAPI()
return helper
