--[[
  smbx2compat core: custom params ("extra settings") for SMBX2's
  ffi_customparams, ported from LunaLua's ConfigPackMiniManager
  (LunaDll/FileManager/config_manager.cpp).

  The editor's config pack (PGE/configs/SMBX2-Integration) names a layout file
  for each object ID (items/npc/npc-N.ini: extra-settings = "foo.json"), and the
  layout's controls carry default values. What scripts get for an object is its
  JSON from the level file merged over those defaults, as
  { "local": defaults for its ID, "global": global_npc.json's defaults }.
  The episode and level folders can replace any of these files.

  Loaded by boot.lua as core.extraSettings; everything runs on first use
  (onStart), because SMBX2's JSON library isn't loaded before main.lua.
]]

local core = __smbx2core

local extraSettings = {}

-- Stands for JSON null in decoded level-file params: a null there clears the default
local NULL = setmetatable({}, { __tostring = function() return "null" end })

local function decode(text, nullv)
    if text == nil or text == "" then
        return nil
    end
    -- arraylen: arrays get [0] = length, so they can be told from objects
    local ok, value = pcall(json.decode, text, nil, nullv, true)
    if ok then
        return value
    end
    return nil
end

local function isObject(v)
    return type(v) == "table" and v ~= NULL and v[0] == nil
end

local function deepCopy(v)
    if type(v) ~= "table" or v == NULL then
        return v
    end
    local ret = {}
    for k, x in pairs(v) do
        ret[k] = deepCopy(x)
    end
    return ret
end

-- mergeJsonSettingsCB: src's values replace dst's, objects merge key by key
local function merge(dst, src)
    if not isObject(src) then
        return dst
    end
    for k, v in pairs(src) do
        if not isObject(dst) then
            dst = {}
        end
        if isObject(v) then
            dst[k] = merge(dst[k], v)
        else
            dst[k] = deepCopy(v)
        end
    end
    return dst
end

local function readFile(path)
    return core.readFile(path)
end

-- "/a/b/../c" -> "/a/c" (the config pack points at ../../../scripts/extra-settings)
local function collapse(path)
    local parts = {}
    for part in string.gmatch(path, "[^/]+") do
        if part == ".." then
            parts[#parts] = nil
        elseif part ~= "." then
            parts[#parts + 1] = part
        end
    end
    return "/" .. table.concat(parts, "/") .. "/"
end

-- Just enough INI: groups[lowercase group][key] = value, quotes and ; comments stripped
local function parseIni(text)
    local groups = {}
    local group
    for line in string.gmatch(text or "", "[^\r\n]+") do
        local name = string.match(line, "^%s*%[([^%]]+)%]")
        if name then
            group = {}
            groups[string.lower(name)] = group
        elseif group then
            local key, value = string.match(line, "^%s*([^=;]-)%s*=%s*(.-)%s*$")
            if key and key ~= "" then
                local quoted = string.match(value, '^"(.-)"')
                if quoted then
                    value = quoted
                else
                    value = string.match(string.gsub(value, ";.*$", ""), "^(.-)%s*$")
                end
                group[key] = value
            end
        end
    end
    return groups
end

local function configPackRoot()
    return core.smbxPath .. "/PGE/configs/SMBX2-Integration/"
end

-- ConfigPackMiniManager::findFile: level folder, then episode folder, then dir
local function findFile(name, dir)
    for _, d in ipairs({ core.customPath, core.episodePath, dir }) do
        local data = readFile(d .. name)
        if data then
            return data
        end
    end
    return nil
end


-------------------------------
-- Layout -> default values  --
-------------------------------

local function toInt(v)
    if type(v) ~= "number" then
        return 0
    end
    return v >= 0 and math.floor(v) or math.ceil(v)
end

local function clamp(v, entry)
    local min, max = entry["value-min"], entry["value-max"]
    if type(min) == "number" and type(max) == "number" then
        if v < min then v = min end
        if v > max then v = max end
    end
    return v
end

local function pathPlus(path, name)
    local ret = {}
    for i, v in ipairs(path) do
        ret[i] = v
    end
    ret[#ret + 1] = name
    return ret
end

local function addType(types, path, kind, name)
    types[kind] = types[kind] or {}
    local list = types[kind]
    list[#list + 1] = pathPlus(path, name)
end

-- make_list_defaults_numeric_list: { {k = key, v = value or nested list}, ... }
local function defaultsList(src)
    local keys = {}
    for k in pairs(src) do
        keys[#keys + 1] = k
    end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    local ret = {}
    for _, k in ipairs(keys) do
        local v = src[k]
        if type(v) == "table" then
            v = defaultsList(v)
        end
        ret[#ret + 1] = { k = tostring(k), v = v }
    end
    return ret
end

local function sized(entry, keys)
    local v = entry["value-default"]
    if type(v) ~= "table" then
        return "<invalid value format>"
    end
    local typ = entry.type or "int"
    if typ ~= "int" and typ ~= "double" then
        return nil
    end
    local ret = {}
    for _, k in ipairs(keys) do
        if v[k] == nil then
            return "<invalid value format>"
        end
        ret[k] = (typ == "int") and toInt(v[k]) or v[k]
    end
    return ret
end

-- read_layout_branches. Controls compare case-insensitively ("spinBox").
local readBranches
readBranches = function(types, dst, src, path)
    for _, entry in ipairs(src) do
        local control = string.lower(tostring(entry.control or "invalid"))
        local name = entry.name or entry.control or "invalid"
        local default = entry["value-default"]

        if control == "group" then
            if entry.children then
                if name ~= ".." then
                    if type(dst[name]) ~= "table" then
                        dst[name] = {}
                    end
                    readBranches(types, dst[name], entry.children, pathPlus(path, name))
                else
                    readBranches(types, dst, entry.children, path)
                end
            end
        elseif control == "spinbox" then
            local typ = entry.type or "int"
            if typ == "int" then
                dst[name] = clamp(toInt(default), entry)
            elseif typ == "double" then
                dst[name] = clamp(type(default) == "number" and default or 0, entry)
            else
                dst[name] = "<invalid value format>"
            end
        elseif control == "checkbox" then
            if default ~= nil then
                dst[name] = default == true
            end
        elseif control == "color" then
            if default ~= nil then
                dst[name] = tostring(default)
                addType(types, path, "color", name)
            end
        elseif control == "combobox" then
            if default ~= nil then
                dst[name] = toInt(default)
            end
        elseif control == "flagbox" then
            if default ~= nil then
                dst[name] = toInt(default)
                addType(types, path, "flags", name)
            end
        elseif control == "description" then
            -- no value
        elseif control == "lineedit" or control == "multilineedit" or control == "file"
            or control == "musicfile" or control == "soundfile" or control == "levelfile" then
            dst[name] = default ~= nil and tostring(default) or ""
        elseif control == "itemselect" then
            if entry.type ~= nil then
                dst[name] = toInt(default)
            end
        elseif control == "sizebox" then
            if default ~= nil then
                local v = sized(entry, { "w", "h" })
                if v then
                    dst[name] = v
                    if type(v) == "table" then
                        addType(types, path, "size", name)
                    end
                else
                    dst[name] = "<invalid value format>"
                end
            end
        elseif control == "pointbox" then
            if default ~= nil then
                local v = sized(entry, { "x", "y" })
                if v then
                    dst[name] = v
                    if type(v) == "table" then
                        addType(types, path, "point", name)
                    end
                else
                    dst[name] = "<invalid value format>"
                end
            end
        elseif control == "rectbox" then
            if default ~= nil then
                local v = sized(entry, { "x", "y", "w", "h" })
                if v then
                    dst[name] = v
                    if type(v) == "table" then
                        addType(types, path, "rect", name)
                    end
                else
                    dst[name] = "<invalid value format>"
                end
            end
        elseif string.sub(control, 1, 9) == "listgroup" then
            if entry.children then
                addType(types, path, "list", name)
                local fields = {}
                readBranches(types, fields, entry.children, pathPlus(path, name))
                dst[name] = { defaults = defaultsList(fields), count = 0, items = {} }
            end
        else
            dst[name] = "<unknown entry type>"
        end
    end
end

-- loadExtraSettings: a layout file's defaults, with "__type" listing the
-- values ffi_customparams converts (colors, points, lists, ...). nil if none.
local function loadLayout(text)
    local layout = decode(text)
    if not isObject(layout) or type(layout.layout) ~= "table" then
        return nil
    end
    local dst = {}
    local types = {}
    readBranches(types, dst, layout.layout, {})
    if next(types) ~= nil then
        dst.__type = types
    end
    return dst
end


------------
-- Stores --
------------
-- One per object kind, read on first use; per-ID defaults are cached as found.

local storeDefs = {
    npc = { ini = "lvl_npc.ini", group = "npc-main", item = "npc", global = "global_npc.json" },
    block = { ini = "lvl_blocks.ini", group = "blocks-main", item = "block", global = "global_block.json" },
    bgo = { ini = "lvl_bgo.ini", group = "background-main", item = "background", global = "global_bgo.json" },
}
local stores = {}

local function getStore(kind)
    local store = stores[kind]
    if store then
        return store
    end
    local def = storeDefs[kind]
    local root = configPackRoot()
    local main = parseIni(readFile(root .. def.ini))[def.group] or {}
    local configDir = main["config-dir"] or "items"
    store = {
        def = def,
        total = tonumber(main.total) or 0,
        setupRoot = collapse(root .. configDir),
        extraRoot = collapse(root .. (main["extra-settings"] or configDir)),
        global = loadLayout(findFile(def.global, root)),
        byId = {},
    }
    stores[kind] = store
    return store
end

-- An ID's layout defaults, or nil (false in the cache) if it has no layout
local function idDefaults(store, id)
    local cached = store.byId[id]
    if cached ~= nil then
        return cached or nil
    end
    local def = store.def
    local iniName = def.item .. "-" .. id .. ".ini"
    local layoutName
    -- config pack, then episode, then level folder; the last one that names a layout wins
    for _, dir in ipairs({ store.setupRoot, core.episodePath, core.customPath }) do
        local text = readFile(dir .. iniName)
        if text then
            local groups = parseIni(text)
            local group = groups[def.item] or groups.general
            if group and group["extra-settings"] then
                layoutName = group["extra-settings"]
            end
        end
    end
    local defaults = layoutName and loadLayout(findFile(layoutName, store.extraRoot))
    store.byId[id] = defaults or false
    return defaults
end


----------------
-- Public API --
----------------

local function encode(v)
    if v == nil then
        return "null"
    end
    return json.encode(v, NULL)
end

local cache = {}

-- mergeExtraSettings for a block, BGO or NPC of the given ID: JSON text, or
-- nil for IDs the config pack doesn't have
function extraSettings.object(kind, id, params)
    params = params or ""
    local key = kind .. id .. "\0" .. params
    local ret = cache[key]
    if ret ~= nil then
        return ret or nil
    end

    local store = getStore(kind)
    if id < 1 or id > store.total then
        cache[key] = false
        return nil
    end

    -- mergeJsonSettingsLG
    local res = { ["local"] = deepCopy(idDefaults(store, id)), global = deepCopy(store.global) }
    local src = decode(params, NULL)
    if isObject(src) then
        if src["local"] ~= nil then
            res["local"] = merge(res["local"], src["local"])
        else
            local srcLocal = {}
            for k, v in pairs(src) do
                if k ~= "global" then
                    srcLocal[k] = v
                end
            end
            res["local"] = merge(res["local"], srcLocal)
        end
        if src.global ~= nil then
            res.global = merge(res.global, src.global)
        end
    end
    -- JSON null where there's nothing, like nlohmann's dump
    res["local"] = res["local"] or NULL
    res.global = res.global or NULL

    ret = encode(res)
    cache[key] = ret
    return ret
end

local sectionDefaults, levelDefaults

-- mergeJsonSettings for the level ("level") or a section ("section")
function extraSettings.simple(kind, params)
    params = params or ""
    local key = kind .. "\0" .. params
    local ret = cache[key]
    if ret ~= nil then
        return ret
    end

    local defaults
    if kind == "section" then
        if sectionDefaults == nil then
            sectionDefaults = loadLayout(findFile("lvl_section.json", configPackRoot())) or false
        end
        defaults = sectionDefaults
    else
        if levelDefaults == nil then
            levelDefaults = loadLayout(findFile("lvl_settings.json", configPackRoot())) or false
        end
        defaults = levelDefaults
    end

    local res = merge(deepCopy(defaults or nil), decode(params, NULL))
    ret = encode(res)
    cache[key] = ret
    return ret
end

return extraSettings
