-- SMBX2COMPAT_DEBUG script for sweep.sh (-l level tests): hold right, jump now and
-- then, screenshot every 400 frames, heartbeat every 300.
-- SWEEP_QUALITY=1|2|3 sets SMATRS's graphics quality (Low/Medium/High) first.
local core = ...
local quality = tonumber(core.getenv("SWEEP_QUALITY") or "")
if quality then
    local saveData = Misc.getCustomEnvironment()._G.SaveData
    saveData.general = saveData.general or {}
    saveData.general.graphicalQuality = quality
    core.log("sweep: graphics quality " .. quality)
end
local level = Level.filename()
local frames = 0
local orig = core.updateRawKeys
core.updateRawKeys = function()
    if frames > 240 then
        player:mem(0xF8, FIELD_BOOL, true) -- right
        if frames % 50 < 20 then
            player:mem(0xFA, FIELD_BOOL, true) -- jump
        end
    end
    orig()
end
local d = {}
function d.onDraw()
    frames = frames + 1
    if frames % 400 == 200 then
        xtech_gfx_screenshot()
    end
    if frames % 300 == 0 then
        core.log(string.format("sweep: %s frame %d player %.0f,%.0f section %d dead %s",
            level, frames, player.x, player.y, player.section, tostring(player.deathTimer)))
    end
end
registerEvent(d, "onDraw")
