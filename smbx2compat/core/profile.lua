--[[
  smbx2compat core: an opt-in timing probe. boot.lua runs this file at the end
  when the SMBX2COMPAT_PROFILE environment variable is set:

    SMBX2COMPAT_PROFILE=1                   frames 100-400 (counted from onStart)
    SMBX2COMPAT_PROFILE=200-800             any other range
    SMBX2COMPAT_PROFILE=100-400,nosample    without the sampling profiler
    SMBX2COMPAT_PROFILE=100-400,memcallers  also which lines read mem() most
                                            (slower; for finding readers only)
    SMBX2COMPAT_PROFILE=load                also samples the level's load: from
                                            here (the end of boot.lua) through
                                            the level's scripts and onStart to
                                            the first frame drawn
    SMBX2COMPAT_PROFILE=spikes              the whole level, and each frame over
    SMBX2COMPAT_PROFILE=spikes=40           25 (40) ms of work, or 50 (80) ms
                                            from the last frame's end, logged
                                            as it happens ("profile spike: ..."),
                                            with its events, slowest listener,
                                            heap change and the functions the
                                            sampler caught in it (up to 80 a
                                            level). On Android: "spikes" as the
                                            first line of smbx2compat-profile.txt
                                            in the Ikoonat folder.

  Over that range it measures, then logs once as "[smbx2core] profile ...":
    - frame time: from the engine's onLoop call to the end of its render
      (vsync is off by default, so this is work, not waiting), and the Lua part;
    - the 5 slowest frames, with their events, traces compiled, heap change
      and the listener that took longest in them;
    - Lua time per event sent by the engine (onTick, onDraw, ...);
    - time per library listener (EventManager.callApiListeners; nested events
      such as onCameraDraw are counted inside the event that sent them);
    - mem() calls per frame (counted in override/.../ffi_mem.lua);
    - the JIT's trace events: traces compiled and aborted (with LuaJIT's
      reason codes, lj_traceerr.h) and every flush of all compiled code;
    - LuaJIT's sampling profiler (jit.profile, 1 ms): how much ran compiled,
      interpreted, in C, in the GC, and the functions and lines that ran most.
      Its function and line attribution is rough: samples taken inside compiled
      code or C calls land on the next point the VM checks, often a caller.

  It wraps functions, so it slows things down a little itself; compare runs
  with the probe on to other runs with the probe on. Run-to-run noise in 1-1
  is about +-0.3 ms a frame, so compare several runs.
]]

local core = __smbx2core
local clock = xtech_misc_clock
local spec = core.profileSpec

local firstFrame, lastFrame = string.match(spec, "^(%d+)%-(%d+)")
local sampling = not string.find(spec, "nosample", 1, true)
local loading = string.find(spec, "load", 1, true) ~= nil
firstFrame, lastFrame = tonumber(firstFrame) or 100, tonumber(lastFrame) or 400
local spikeMs = string.match(spec, "spikes=?(%d*)")
if spikeMs then
    spikeMs = tonumber(spikeMs) or 25
    if not string.find(spec, "^%d+%-%d+") then
        firstFrame, lastFrame = 1, math.huge
    end
end
local spikesLogged, lastFrameEnd = 0, nil
local frameSamples = {} -- this frame's sampled functions (spikes)

local frame = 0         -- frames since onStart
local active = false    -- inside the measured range
local reported = false

local frames, skipped = 0, 0
local frameStart, frameLua = nil, 0
local busyTotal, busyMax, luaTotal = 0, 0, 0
local slowest = {} -- the 5 slowest frames: {busy, lua, frame, traces compiled in it}
local stopsAtFrameStart = 0
local eventTime, eventOrder = {}, {}
local frameEvents, gcAtFrameStart = {}, 0 -- this frame's events, and the Lua heap size
local listenerTime, listenerCalls = {}, {}
local frameTopListener, frameTopTime = "", 0 -- this frame's slowest listener call
local memCalls = core.profileMemCalls -- {get = n, set = n, string = n}
local memCallers = core.profileMemCallers -- "line < library line" -> reads (sampled)

local function log(msg)
    xtech_misc_log("[smbx2core] profile " .. msg)
end

local function addEvent(name, dt)
    if not eventTime[name] then
        eventTime[name] = 0
        eventOrder[#eventOrder + 1] = name
    end
    eventTime[name] = eventTime[name] + dt
    frameEvents[#frameEvents + 1] = name .. " " .. string.format("%.1f", dt * 1000)
end


-----------------------
-- Sampling profiler --
-----------------------

local jitProfile = require("jit.profile")
local vmSamples = {N = 0, I = 0, C = 0, G = 0, J = 0}
local selfFuncs, selfLines, inclFuncs = {}, {}, {}
local totalSamples = 0

-- Trace compiler events: a healthy run compiles early and then mostly stops
local traceEvents = {start = 0, stop = 0, abort = 0, flush = 0}
local abortReasons = {}
-- LuaJIT's trace error codes, in lj_traceerr.h order (code 0 first)
local traceErrNames = {
    [0] = "RECERR", "TRACEUV", "TRACEOV", "STACKOV", "SNAPOV", "BLACKL", "RETRY",
    "NYIBC", "LLEAVE", "LINNER", "LUNROLL", "BADTYPE", "CJITOFF", "CUNROLL",
    "DOWNREC", "NYIFFU", "NYIRETL", "STORENN", "NOMM", "IDXLOOP", "NYITMIX",
    "NOCACHE", "NYICONV", "NYICALL", "GFAIL", "PHIOV", "TYPEINS", "MCODEAL",
    "MCODEOV", "MCODELM", "SPILLOV", "BADRA", "NYIIR", "NYIPHI", "NYICOAL",
}
local abortCodes = {}

-- A flush throws away all compiled code; each is logged with how much there was
local liveTraces, liveBytes, flushesLogged = 0, 0, 0

local function onTrace(what, tr, func, pc, otr, oex)
    traceEvents[what] = (traceEvents[what] or 0) + 1
    if what == "stop" then
        local mcode = core.jitUtil.tracemc(tr)
        liveTraces = liveTraces + 1
        liveBytes = liveBytes + (mcode and #mcode or 0)
    elseif what == "flush" then
        if flushesLogged < 10 then
            flushesLogged = flushesLogged + 1
            log(string.format("JIT flush at frame %d, %d traces (%.0f KB) compiled since the last one",
                frame, liveTraces, liveBytes / 1024))
        end
        liveTraces, liveBytes = 0, 0
    elseif what == "abort" then
        local name = traceErrNames[otr] or ("error " .. tostring(otr))
        abortCodes[name] = (abortCodes[name] or 0) + 1
        local info = func and core.jitUtil.funcinfo(func, pc)
        local where = info and info.loc or "?"
        local key = name .. " @ " .. string.gsub(where, "^.*/", "")
        abortReasons[key] = (abortReasons[key] or 0) + 1
    end
end

local function sample(thread, samples, vmstate)
    totalSamples = totalSamples + samples
    vmSamples[vmstate] = (vmSamples[vmstate] or 0) + samples
    local f = jitProfile.dumpstack(thread, "F", 1)
    selfFuncs[f] = (selfFuncs[f] or 0) + samples
    if spikeMs then
        local key = f .. (vmstate == "G" and " (GC)" or "")
        frameSamples[key] = (frameSamples[key] or 0) + samples
    end
    local l = jitProfile.dumpstack(thread, "l", 1)
    selfLines[l] = (selfLines[l] or 0) + samples
    -- Each function once per sample, however deep the recursion
    local seen = {}
    for name in string.gmatch(jitProfile.dumpstack(thread, "F;", -40), "[^;]+") do
        if not seen[name] then
            seen[name] = true
            inclFuncs[name] = (inclFuncs[name] or 0) + samples
        end
    end
end


---------------
-- Reporting --
---------------

local function ms(seconds)
    return string.format("%.2f", seconds * 1000)
end

local function top(counts, n)
    local list = {}
    for k, v in pairs(counts) do
        list[#list + 1] = {k, v}
    end
    table.sort(list, function(a, b) return a[2] > b[2] end)
    local out = {}
    for i = 1, math.min(n, #list) do
        out[i] = list[i]
    end
    return out
end

-- The sampler's results so far (prefix: "load " for the load's), then a fresh start
local function logSamples(prefix)
    if totalSamples > 0 then
        local function pct(n)
            return string.format("%.0f%%", 100 * n / totalSamples)
        end
        log(string.format("%ssamples %d: compiled %s, interpreted %s, C %s, GC %s, JIT compiler %s",
            prefix, totalSamples, pct(vmSamples.N), pct(vmSamples.I), pct(vmSamples.C), pct(vmSamples.G), pct(vmSamples.J)))
        for _, which in ipairs({{"self", selfFuncs}, {"incl", inclFuncs}, {"line", selfLines}}) do
            local parts = {}
            for _, e in ipairs(top(which[2], prefix == "" and 15 or 45)) do
                parts[#parts + 1] = e[1] .. " " .. pct(e[2])
            end
            log(prefix .. "top " .. which[1] .. ": " .. table.concat(parts, ", "))
        end
    end
    vmSamples = {N = 0, I = 0, C = 0, G = 0, J = 0}
    selfFuncs, selfLines, inclFuncs = {}, {}, {}
    totalSamples = 0
end

local function report()
    if reported then
        return
    end
    reported = true
    jit.attach(onTrace) -- before stopping the profiler, which flushes too
    if sampling then
        jitProfile.stop()
    end
    if frames == 0 then
        log("no frames measured (range " .. firstFrame .. "-" .. lastFrame .. ", reached frame " .. frame .. ")")
        return
    end

    log(string.format("frames %d-%d: %d measured, %d without a render; jit %s",
        firstFrame, lastFrame, frames, skipped, jit.status() and "on" or "OFF"))
    log("frame ms: avg " .. ms(busyTotal / frames) .. ", max " .. ms(busyMax)
        .. "; Lua avg " .. ms(luaTotal / frames)
        .. "; engine avg " .. ms((busyTotal - luaTotal) / frames))

    local parts = {}
    for _, f in ipairs(slowest) do
        parts[#parts + 1] = string.format("frame %d %s ms (Lua %s, %d traces compiled, heap %+.0f KB, slowest listener %s %s: %s)",
            f[3], ms(f[1]), ms(f[2]), f[4], f[6], f[7], ms(f[8]), f[5])
    end
    log("slowest: " .. table.concat(parts, "; "))

    parts = {}
    for _, name in ipairs(eventOrder) do
        parts[#parts + 1] = name .. " " .. ms(eventTime[name] / frames)
    end
    log("Lua ms per frame by event: " .. table.concat(parts, ", "))

    log(string.format("mem() calls per frame: %.0f reads, %.0f writes, %.0f strings",
        memCalls.get / frames, memCalls.set / frames, memCalls.string / frames))
    if memCallers then
        local parts = {}
        for _, e in ipairs(top(memCallers, 25)) do
            parts[#parts + 1] = string.format("%s %.0f", e[1], e[2] / frames)
        end
        log("mem() reads per frame by caller (sampled): " .. table.concat(parts, "; "))
    end

    parts = {}
    for _, e in ipairs(top(listenerTime, 15)) do
        parts[#parts + 1] = string.format("%s %s (%.0f/frame)", e[1], ms(e[2] / frames), listenerCalls[e[1]] / frames)
    end
    log("top listeners, ms per frame: " .. table.concat(parts, ", "))

    log(string.format("traces per frame: %.1f started, %.1f compiled, %.1f aborted; %d flushes",
        traceEvents.start / frames, traceEvents.stop / frames, traceEvents.abort / frames, traceEvents.flush))
    parts = {}
    for _, e in ipairs(top(abortReasons, 12)) do
        parts[#parts + 1] = e[1] .. " x" .. e[2]
    end
    if #parts > 0 then
        log("top trace aborts: " .. table.concat(parts, "; "))
    end
    parts = {}
    for _, e in ipairs(top(abortCodes, 40)) do
        parts[#parts + 1] = e[1] .. " x" .. e[2]
    end
    if #parts > 0 then
        log("trace aborts by code: " .. table.concat(parts, ", "))
    end

    logSamples("")
end


-----------
-- Hooks --
-----------

-- Engine events (boot.lua's sendEvent looks __callEvent up on every call)
local callEvent = __callEvent
function __callEvent(e, ...)
    if not active then
        return callEvent(e, ...)
    end
    local t = clock()
    callEvent(e, ...)
    addEvent(e.eventName, clock() - t)
end

local doEventQueue = __doEventQueue
function __doEventQueue(...)
    if not active then
        return doEventQueue(...)
    end
    local t = clock()
    doEventQueue(...)
    addEvent("eventQueue", clock() - t)
end

-- Library listeners: main_events.lua's callApiListeners, timed per handler
do
    local names = {}
    local function listenerName(api, handler)
        local f = api[handler]
        local name = names[f]
        if not name then
            local info = debug.getinfo(f, "S")
            name = string.gsub(info.short_src, "^.*/", "") .. ":" .. handler
            names[f] = name
        end
        return name
    end

    local EventManager = EventManager
    local callApiListeners = EventManager.callApiListeners
    EventManager.callApiListeners = function(name, isBefore, args)
        if not active then
            return callApiListeners(name, isBefore, args)
        end
        -- Same loop as main_events.lua, with a clock around each call
        local listenerTbl = EventManager.apiListeners[isBefore][name]
        if (listenerTbl ~= nil) then
            for _, nextAPIToHandle in ipairs(listenerTbl) do
                local hostObject = nextAPIToHandle.api
                if hostObject[nextAPIToHandle.eventHandlerName] ~= nil then
                    local key = listenerName(hostObject, nextAPIToHandle.eventHandlerName)
                    local t = clock()
                    hostObject[nextAPIToHandle.eventHandlerName](args[1], args[2], args[3], args[4])
                    t = clock() - t
                    listenerTime[key] = (listenerTime[key] or 0) + t
                    if t > frameTopTime then
                        frameTopListener, frameTopTime = key, t
                    end
                    listenerCalls[key] = (listenerCalls[key] or 0) + 1
                end
            end
        end
    end
end

-- Frames: onLoop starts one, the end of onRenderEnd closes it. Lua time is
-- the time spent inside the engine's hooks.
local onLoop, onRenderStart, onRenderEnd, onExit =
    __xtech_onLoop, __xtech_onRenderStart, __xtech_onRenderEnd, __xtech_onExit

function __xtech_onLoop(...)
    frame = frame + 1
    if frame == firstFrame then
        active = true
        memCalls.get, memCalls.set, memCalls.string = 0, 0, 0
        if memCallers then
            for k in pairs(memCallers) do
                memCallers[k] = nil
            end
        end
        if sampling then
            jitProfile.start("i1", sample)
        end
        jit.attach(onTrace, "trace")
    end
    if not active then
        return onLoop(...)
    end
    if frameStart then
        skipped = skipped + 1 -- the last frame wasn't drawn
    end
    frameStart = clock()
    stopsAtFrameStart = traceEvents.stop
    frameEvents, gcAtFrameStart = {}, collectgarbage("count")
    frameTopListener, frameTopTime = "none", 0
    if spikeMs then
        frameSamples = {}
    end
    onLoop(...)
    frameLua = clock() - frameStart
end

-- The hooks between onLoop and onRenderEnd only add Lua time
local function timedHook(f)
    return function(...)
        if not active then
            return f(...)
        end
        local t = clock()
        f(...)
        frameLua = frameLua + clock() - t
    end
end
__xtech_onInputUpdate = timedHook(__xtech_onInputUpdate) -- onInputUpdate, onTick
__xtech_onRenderStart = timedHook(onRenderStart)
__xtech_onCameraUpdate = timedHook(__xtech_onCameraUpdate)
__xtech_onDraw = timedHook(__xtech_onDraw)
__xtech_onCameraDraw = timedHook(__xtech_onCameraDraw)
__xtech_onHUDDraw = timedHook(__xtech_onHUDDraw)

local loadStart = loading and clock()
if loading then
    jitProfile.start("i1", sample)
end

function __xtech_onRenderEnd(...)
    local t = clock()
    onRenderEnd(...)
    if loadStart then
        jitProfile.stop()
        log(string.format("load: %.0f ms from the end of boot.lua to the first frame drawn", (t - loadStart) * 1000))
        logSamples("load ")
        loadStart = nil
    end
    if active and frameStart then
        frameLua = frameLua + clock() - t
        local busy = clock() - frameStart
        frames = frames + 1
        busyTotal = busyTotal + busy
        luaTotal = luaTotal + frameLua
        local compiled = traceEvents.stop - stopsAtFrameStart
        if #slowest < 5 or busy > slowest[#slowest][1] then
            slowest[#slowest + 1] = {busy, frameLua, frame, compiled,
                table.concat(frameEvents, " "), collectgarbage("count") - gcAtFrameStart,
                frameTopListener, frameTopTime}
            table.sort(slowest, function(a, b) return a[1] > b[1] end)
            slowest[6] = nil
        end
        if busy > busyMax then
            busyMax = busy
        end
        -- spikes: work over the limit, or a long wait since the last frame
        -- (the driver, presenting, the engine outside these hooks)
        local now = clock()
        local wall = lastFrameEnd and (now - lastFrameEnd) or 0
        lastFrameEnd = now
        if spikeMs and spikesLogged < 80 and (busy * 1000 >= spikeMs or wall * 1000 >= 2 * spikeMs) then
            spikesLogged = spikesLogged + 1
            local parts = {}
            for _, e in ipairs(top(frameSamples, 6)) do
                parts[#parts + 1] = e[1] .. " " .. e[2]
            end
            log(string.format("spike: frame %d, %s ms work (Lua %s), %s ms since the last frame; %d traces compiled, " ..
                              "heap %+.0f KB, slowest listener %s %s; events: %s; sampled: %s",
                frame, ms(busy), ms(frameLua), ms(wall), compiled, collectgarbage("count") - gcAtFrameStart,
                frameTopListener, ms(frameTopTime), table.concat(frameEvents, " "),
                #parts > 0 and table.concat(parts, ", ") or "nothing"))
        end
        frameStart = nil
    end
    if active and frame >= lastFrame then
        active = false
        report()
    end
end

function __xtech_onExit(...)
    if not reported then
        active = false
        report()
    end
    return onExit(...)
end

if spikeMs then
    xtech_misc_log("[smbx2core] profiling: logging frames over " .. spikeMs .. " ms" ..
                   (lastFrame == math.huge and "" or (" in frames " .. firstFrame .. "-" .. lastFrame)))
else
    xtech_misc_log("[smbx2core] profiling frames " .. firstFrame .. "-" .. lastFrame)
end
