-- Loading screen probe (SMBX2COMPAT, temporary hook; found the JIT-trace
-- retention fixed 2026-11-06). Run in the loading screen's state after
-- init: lists which earlier loads' contexts (childEnvsByGlobal keys) are
-- still alive after a full collection, and one strong path to the oldest
-- from the registry (weak entries skipped).
-- Hook it into core/loadscreen.lua (don't commit the hook):
--   top, after getupvalue:
--     local PROBE_DBG, PROBE = debug, os.getenv("LSPROBE")
--     local PROBE_ENV = {type=type, next=next, tostring=tostring, tonumber=tonumber,
--       string=string, table=table, rawequal=rawequal, jit=jit, setfenv=setfenv,
--       loadstring=loadstring, collectgarbage=collectgarbage, assert=assert}
--     local PROBE_WATCH, PROBE_LOAD = setmetatable({}, {__mode="k"}), 0
--   in __xtech_loadScreenInit, before dropOldContexts():
--     if PROBE and childEnvsByGlobal then for k in pairs(childEnvsByGlobal) do
--       if not bootContexts[k] and not PROBE_WATCH[k] then PROBE_WATCH[k] = PROBE_LOAD end end end
--   after it: PROBE_LOAD = PROBE_LOAD + 1; after call(init):
--     if PROBE then local E = PROBE_ENV; E.collectgarbage(); E.collectgarbage()
--       E.setfenv(E.assert(E.loadstring(core.readFile(PROBE), "=lsprobe")), E)(core, PROBE_DBG, PROBE_WATCH, PROBE_LOAD) end
-- (SMBX2's lockdown removes type, setfenv, ... from the globals by then.)
-- Compare cdata with rawequal: LuaJIT's cdata == can be true against a table.
local core, dbg, watch, loadNo = ...
local alive, oldest, oldestNo = {}, nil, nil
for g, no in next, watch do
    alive[#alive + 1] = no
    if no < loadNo and (not oldestNo or no < oldestNo) then oldest, oldestNo = g, no end
end
core.log("lsprobe: load " .. loadNo .. ", contexts alive from loads: " .. table.concat(alive, ","))
if not oldest then return end
local function weakmode(t)
    local mt = dbg.getmetatable(t)
    local m = mt and mt.__mode
    if type(m) ~= "string" then return false, false end
    return m:find("k") ~= nil, m:find("v") ~= nil
end
local seen = { [watch] = true }
local queue = { { dbg.getregistry(), "REG" } }
seen[queue[1][1]] = true
local head, hits = 1, 0
local function push(v, path)
    if rawequal(v, oldest) then
        hits = hits + 1
        core.log("lsprobe: load " .. oldestNo .. "'s context <- " .. path)
        return
    end
    local t = type(v)
    if (t == "table" or t == "function" or t == "userdata" or t == "thread") and not seen[v] then
        seen[v] = true
        queue[#queue + 1] = { v, path }
    end
end
local function key(k)
    if type(k) == "string" then return k end
    return "[" .. tostring(k) .. "]"
end
while head <= #queue and hits < 6 do
    local v, path = queue[head][1], queue[head][2]
    head = head + 1
    if #path < 600 then
        local t = type(v)
        if t == "table" then
            local mt = dbg.getmetatable(v)
            if mt then push(mt, path .. ".<mt>") end
            local wk, wv = weakmode(v)
            for k, val in next, v do
                if not wk then push(k, path .. ".<key " .. key(k) .. ">") end
                if not wv then push(val, path .. "." .. key(k)) end
            end
        elseif t == "function" then
            local i = 1
            while true do
                local name, val = dbg.getupvalue(v, i)
                if name == nil then break end
                push(val, path .. ".<up " .. name .. ">")
                i = i + 1
            end
            push(dbg.getfenv(v), path .. ".<env>")
        elseif t == "userdata" then
            local mt = dbg.getmetatable(v)
            if mt then push(mt, path .. ".<mt>") end
            push(dbg.getfenv(v), path .. ".<env>")
        elseif t == "thread" then
            local lvl = 0
            while dbg.getinfo(v, lvl, "f") do
                local i = 1
                while true do
                    local name, val = dbg.getlocal(v, lvl, i)
                    if name == nil then break end
                    push(val, path .. ".<co L" .. lvl .. " " .. name .. ">")
                    i = i + 1
                end
                push(dbg.getinfo(v, lvl, "f").func, path .. ".<co L" .. lvl .. " func>")
                lvl = lvl + 1
            end
        end
    end
end
if hits == 0 then core.log("lsprobe: no strong path from the registry (" .. head .. " objects walked)") end
