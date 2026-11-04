--[[
  smbx2compat core: replaces SMBX2's base/engine/ffi_mem.lua, which reads and
  writes SMBX 1.3's process memory through raw pointers. TheXTech emulates the
  global part of that memory (src/script/luna/mememu.cpp); object memory
  (players, NPCs, blocks, ...) is Phase 2.
]]

local core = __smbx2core
local mem_get = xtech_mem_get
local mem_set = xtech_mem_set
local FIELD_WORD = FIELD_WORD
local FIELD_STRING = FIELD_STRING
local FIELD_BOOL = FIELD_BOOL

-- mememu.h: GM_BASE..GM_END
local GLOBAL_FIRST = 0x00B25000
local GLOBAL_LAST  = 0x00B2E000

local function isGlobal(addr)
    return addr >= GLOBAL_FIRST and addr <= GLOBAL_LAST
end

-- One "missing" entry for all object memory; the first address is logged for a clue
local function objectMemory(what, addr)
    if not core.missing["mem() " .. what] then
        core.warn(string.format("first mem() %s outside globals: 0x%08X", what, addr))
    end
    core.missingAPI("mem() " .. what)
end

local function readmem(addr, dtype)
    if not isGlobal(addr) then
        objectMemory("reads", addr)
        return (dtype == FIELD_BOOL) and false or 0
    end
    if (dtype == FIELD_STRING) then
        core.missingAPI("mem() strings")
        return ""
    elseif (dtype == FIELD_BOOL) then
        return mem_get(addr, FIELD_WORD) ~= 0
    end
    return mem_get(addr, dtype)
end

local function writemem(addr, dtype, val)
    if not isGlobal(addr) then
        objectMemory("writes", addr)
        return
    end
    if (dtype == FIELD_STRING) then
        core.missingAPI("mem() strings")
    elseif (dtype == FIELD_BOOL) then
        mem_set(addr, FIELD_WORD, (val and val ~= 0) and -1 or 0)
    else
        mem_set(addr, dtype, val)
    end
end

local function mem(addr, dtype, val)
    if (val == nil) then
        return readmem(addr, dtype)
    else
        return writemem(addr, dtype, val)
    end
end

--------------------
-- Assign globals --
--------------------
_G.readmem = readmem
_G.writemem = writemem
_G.mem = mem
