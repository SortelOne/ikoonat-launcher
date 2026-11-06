-- SMBX2COMPAT_DEBUG file: calls Misc.exitEngine() after EXITTEST_FRAME frames
-- (default 300) in levels whose name contains EXITTEST_LEVEL (default: any)
local core = ...
local at = tonumber(core.getenv("EXITTEST_FRAME") or "") or 300
local only = core.getenv("EXITTEST_LEVEL")
local level = Level.filename()
local frames = 0
local orig = core.updateRawKeys
core.updateRawKeys = function(...)
    frames = frames + 1
    if frames == at and (not only or level:find(only, 1, true)) then
        core.log("exittest: calling Misc.exitEngine in " .. level)
        Misc.exitEngine()
    end
    return orig(...)
end
core.log("exittest: in " .. level)
