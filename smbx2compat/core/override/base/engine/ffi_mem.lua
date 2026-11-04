--[[
  smbx2compat core: replaces SMBX2's base/engine/ffi_mem.lua, which reads and
  writes SMBX 1.3's process memory through raw pointers. Here every address
  goes to the engine (xtech_mem_get/set in xtech_lua_bindings.cpp): globals go
  to TheXTech's memory emulator (src/script/luna/mememu.cpp), and the object
  arrays (players, NPCs, blocks, BGOs, layers, events, ...) to the matching
  object's fields. Strings (VB6 string pointers in SMBX) are read and written
  by address too. The engine logs each address it doesn't know once.

  Reads go through LuaJIT's FFI, as a call through a C function pointer the
  engine provides (__xtech_ffiMemGet): LuaJIT compiles those into its traces,
  but not luabind calls, and SMBX2 reads ~17,000 fields a frame in SMATRS 1-1.
]]

local ffi = ffi -- main.lua gives its low-level libraries the FFI as a global
local mem_get = ffi.cast("double (*)(uint32_t, int32_t)", __xtech_ffiMemGet)
local mem_set = xtech_mem_set
local mem_getString = xtech_mem_getString
local mem_setString = xtech_mem_setString
local FIELD_WORD = FIELD_WORD
local FIELD_STRING = FIELD_STRING
local FIELD_BOOL = FIELD_BOOL

-- With SMBX2COMPAT_PROFILE set, count calls for core/profile.lua
local counts = __smbx2core.profileMemCalls
if counts then
    local get, set, getString, setString = mem_get, mem_set, mem_getString, mem_setString
    mem_get = function(a, t) counts.get = counts.get + 1 return get(a, t) end
    mem_set = function(a, t, v) counts.set = counts.set + 1 return set(a, t, v) end
    mem_getString = function(a) counts.string = counts.string + 1 return getString(a) end
    mem_setString = function(a, v) counts.string = counts.string + 1 return setString(a, v) end
end

local function readmem(addr, dtype)
    if (dtype == FIELD_STRING) then
        return mem_getString(addr)
    elseif (dtype == FIELD_BOOL) then
        return mem_get(addr, FIELD_WORD) ~= 0
    end
    return mem_get(addr, dtype)
end

local function writemem(addr, dtype, val)
    if (dtype == FIELD_STRING) then
        mem_setString(addr, tostring(val))
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
