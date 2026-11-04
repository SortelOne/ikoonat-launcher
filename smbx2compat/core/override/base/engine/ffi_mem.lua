--[[
  smbx2compat core: replaces SMBX2's base/engine/ffi_mem.lua, which reads and
  writes SMBX 1.3's process memory through raw pointers. Here every address
  goes to the engine (xtech_mem_get/set in xtech_lua_bindings.cpp): globals go
  to TheXTech's memory emulator (src/script/luna/mememu.cpp), and the object
  arrays (players, NPCs, blocks, BGOs) to the matching object's fields. The
  engine logs each address it doesn't know once.
]]

local core = __smbx2core
local mem_get = xtech_mem_get
local mem_set = xtech_mem_set
local FIELD_WORD = FIELD_WORD
local FIELD_STRING = FIELD_STRING
local FIELD_BOOL = FIELD_BOOL

local function readmem(addr, dtype)
    if (dtype == FIELD_STRING) then
        core.missingAPI("mem() strings")
        return ""
    elseif (dtype == FIELD_BOOL) then
        return mem_get(addr, FIELD_WORD) ~= 0
    end
    return mem_get(addr, dtype)
end

local function writemem(addr, dtype, val)
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
