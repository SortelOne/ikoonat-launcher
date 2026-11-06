-- SMBX2COMPAT_DEBUG script for SMATRS (run it with episode.sh): visit levels from
-- the map, as SMATRS's level menu enters them (levelEnterMenu.lua's
-- levelEnterRoutine, without the menu). The list is levels.txt beside this file:
--   line 1: frames per level [frames between a level's screenshots, default 400]
--   then one entry per line: a level file name, "story=N" (SaveData.storyProgress;
--   the map's layers follow on its next visit) or "section=N" (move the map to
--   section N as fast travel does; 1 = the daredevil area, from a map that has
--   left the start menu, so after a level).
-- In each level: hold right, jump now and then, screenshots, back to the map at
-- the frame count with Level.load (no save). A map visit takes a screenshot at
-- frame 250. The episode's start menu closes once a level has been entered.
local core = ...
local env = Misc.getCustomEnvironment()
local req = env._G.require
local GameData, SaveData = env._G.GameData, env._G.SaveData
local level = Level.filename()
local MAP = "World Map.lvlx"
local LIST = (core.debugSpec or ""):gsub("[^/\\]*$", "") .. "levels.txt"
local listLines = {}
for line in string.gmatch(core.readFile(LIST) or "", "[^\r\n]+") do listLines[#listLines + 1] = line end
-- first line: frames per level [frames between a level's screenshots, default 400]
local LEVEL_FRAMES = tonumber((listLines[1] or ""):match("^%s*(%d+)")) or 1500
local SHOT_EVERY = tonumber((listLines[1] or ""):match("^%s*%d+%s+(%d+)")) or 400
local frames = 0

if not GameData.epsweep then
    local q = {}
    for i = 2, #listLines do
        q[#q + 1] = listLines[i]
    end
    GameData.epsweep = { queue = q, n = 0 }
end
local st = GameData.epsweep
core.log(string.format("epsweep: in %s (story %s), %d left", level, tostring(SaveData.storyProgress), #st.queue - st.n))

local orig = core.updateRawKeys
core.updateRawKeys = function()
    if level ~= MAP and frames > 240 then
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
    if level == MAP then
        if frames == 250 or (st.shotsAt and st.shotsAt[frames]) then
            core.log(string.format("epsweep: map screenshot (frame %d, section %d)", frames, player.section))
            xtech_gfx_screenshot()
        end
        if frames == (st.mapNext or 300) then
            st.mapNext = nil
            st.n = st.n + 1
            local name = st.queue[st.n]
            if not name then
                core.log("epsweep: DONE")
                return
            end
            -- "story=N": set SaveData.storyProgress (layers change on the next map visit)
            local story = name:match("^story=(%d+)$")
            if story then
                SaveData.storyProgress = tonumber(story)
                core.log("epsweep: story progress " .. story)
                st.mapNext = frames + 1
                return
            end
            -- "section=N": fast-travel the map to section N (as fastTravel.lua's menu),
            -- screenshots after 200, 400 and 600 frames
            local section = name:match("^section=(%d+)$")
            if section then
                core.log("epsweep: map to section " .. section)
                -- fastTravel.warpRoutine needs the open menu; this is its section load,
                -- then the daredevil option's Lakitu drop-off (World Map/luna.lua)
                local map = req("_scripts/smatrs/map")
                local R = env._G.Routine
                R.run(function()
                    map.queueSectionLoad(tonumber(section))
                    R.waitSignal("smatrsMap_sectionLoaded")
                    if tonumber(section) == 1 then
                        map.queueEvent("lakituDropoff", {
                            landPosition = req("_scripts/smatrs/map_points").getPoint("daredevilWarpPoint"),
                        })
                    end
                end)
                st.shotsAt = {[frames + 200] = true, [frames + 400] = true, [frames + 600] = true}
                st.mapNext = frames + 650
                return
            end
            core.log("epsweep: entering " .. name)
            st.leaving = nil
            local general = req("_scripts/generalStuff")
            general.setLevelCheckpoint(name, 0)
            GameData.levelData = GameData.levelData or {}
            GameData.levelData[name] = GameData.levelData[name] or {}
            GameData.levelData[name].hasAlwaysBeenDaredevil = nil
            GameData.inTimeTrials = false
            GameData.menuMode = false -- as leaving the start menu does (mainMenu.lua)
            SaveData.lastEnteredMapLevel = name
            Level.load(name)
        end
        return
    end
    if frames % SHOT_EVERY == math.floor(SHOT_EVERY / 2) then
        xtech_gfx_screenshot()
    end
    if frames % 300 == 0 then
        core.log(string.format("epsweep: %s frame %d player %.0f,%.0f section %d",
            level, frames, player.x, player.y, player.section))
    end
    -- A death restarts the level (SMATRS's playerHarm.lua) over our Level.load:
    -- leave again at once when we come back to the level we were leaving
    if frames == LEVEL_FRAMES or (st.leaving == level and frames == 2) then
        core.log("epsweep: back to the map from " .. level)
        st.leaving = level
        Level.load(MAP)
    end
end
registerEvent(d, "onDraw")
