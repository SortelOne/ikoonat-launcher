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
  but not luabind calls, and SMBX2 reads ~48,000 fields a frame in SMATRS 1-1.

  Results are returned from a local rather than as "return f(...)": LuaJIT
  counts each tail call in a trace against its loop-unroll limit (15), so a
  trace that read mem() more than a few times through them was thrown away.
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
    mem_get = function(a, t) counts.get = counts.get + 1 local v = get(a, t) return v end
    -- ",memcallers": every 97th read, which line asked (skipping mem() itself and
    -- SMBX2's generated field getters, "=!Block:x"), and the first line outside
    -- base/engine (the library that wanted the field)
    local callers, getinfo = __smbx2core.profileMemCallers, __smbx2core.getinfo
    if callers then
        local function where(from, skip)
            for level = from, from + 12 do
                local info = getinfo(level, "Sl")
                if not info then
                    return "?", level
                end
                local src = info.short_src
                if not skip(src) then
                    return string.gsub(src, "^.*/", "") .. ":" .. info.currentline, level
                end
            end
            return "?", from
        end
        local function isMem(src)
            return string.find(src, "ffi_mem.lua", 1, true) or string.sub(src, 1, 1) == "!"
        end
        local function isEngine(src)
            return isMem(src) or string.find(src, "base/engine/", 1, true)
        end
        local countedGet = mem_get
        mem_get = function(a, t)
            if counts.get % 97 == 0 then
                local first, level = where(3, isMem)
                local user = where(level, isEngine)
                local key = (user == first) and first or (first .. " < " .. user)
                callers[key] = (callers[key] or 0) + 97
            end
            local v = countedGet(a, t)
            return v
        end
    end
    mem_set = function(a, t, v) counts.set = counts.set + 1 set(a, t, v) end
    mem_getString = function(a) counts.string = counts.string + 1 local v = getString(a) return v end
    mem_setString = function(a, v) counts.string = counts.string + 1 setString(a, v) end
end

local function readmem(addr, dtype)
    local v
    if (dtype == FIELD_STRING) then
        v = mem_getString(addr)
    elseif (dtype == FIELD_BOOL) then
        v = mem_get(addr, FIELD_WORD) ~= 0
    else
        v = mem_get(addr, dtype)
    end
    return v
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
        local v = readmem(addr, dtype)
        return v
    end
    writemem(addr, dtype, val)
end

--------------------
-- Assign globals --
--------------------
_G.readmem = readmem
_G.writemem = writemem
_G.mem = mem
