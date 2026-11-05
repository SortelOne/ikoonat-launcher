--[[
  smbx2compat core: stands in for LunaDll.dll. SMBX2's ffi_*.lua files declare
  the DLL's exports with ffi.cdef and call them through ffi.load("LunaDll.dll");
  boot.lua hands them this table instead, so those files run unmodified.

  Exports are implemented in Lua over the engine's xtech_* bindings. Their C
  side is LunaDll/LuaMain/LuaProxyFFI*.cpp in the LunaLua source. An export we
  don't have yet logs once when called and returns nil.
]]

local core = __smbx2core
local ffi = require("ffi")

local dll = {}


----------------------
-- ffi_misc.lua     --
----------------------

-- Misc.GetKeyState reads this array directly (Windows virtual-key codes).
-- TODO: fill from the engine's keyboard state.
local keyStates = ffi.new("unsigned char[256]")
function dll.LunaLuaGetKeyStateArray()
    return keyStates
end

-- Milliseconds per engine tick. TheXTech runs SMBX's 64.1 fps, like LunaLua's default.
local frameTimingMs = 15.6
function dll.LunaLuaGetFrameTiming()
    return frameTimingMs
end

-- GameData: a string scripts keep for the whole episode. It lives in the
-- engine (xtech_misc_*GameData) because each level gets a new Lua state.
function dll.LunaLuaSetGameData(dataPtr, dataLen)
    xtech_misc_setGameData(ffi.string(dataPtr, dataLen))
end

-- GameDataStruct is { int len; char data[0]; }: build it in one buffer, kept
-- alive until ffi_misc hands it back to LunaLuaFreeReturnedGameData
local returnedGameData = {}
function dll.LunaLuaGetGameData()
    local data = xtech_misc_getGameData()
    local buf = ffi.new("uint8_t[?]", ffi.sizeof("int") + #data)
    local ret = ffi.cast("GameDataStruct*", buf)
    ret.len = #data
    ffi.copy(ret.data, data, #data)
    returnedGameData[tostring(ret)] = buf
    return ret
end

function dll.LunaLuaFreeReturnedGameData(ptr)
    returnedGameData[tostring(ptr)] = nil
end

-- Misc._getDefine: LunaLua patches these SMBX constants (coin values, grab
-- rules, ...) into the game. We remember them so reads return what was set.
-- TODO: apply them to the engine.
local defines = {}
local function defineAccessor(name)
    local field, kind = string.match(name, "^LunaLua_Defines__(.+)__([gs]et)$")
    if not field then
        return nil
    end
    if kind == "get" then
        return function()
            return defines[field] or 0
        end
    end
    return function(value)
        core.missingAPI("Defines values (kept, not applied to the engine)")
        defines[field] = value
    end
end

function dll.LunaLua_Defines_mem_set(address, value)
    core.missingAPI("Defines memory writes")
    return false
end

function dll.LunaLuaSetWeakLava(value)
    core.missingAPI("weak lava setting")
end

function dll.LunaLuaGetWeakLava()
    return false
end

-- Analog stick of the player's controller, -32768..32767 per axis.
-- TODO: from the engine's controls; centered until then.
function dll.LunaLuaGetSelectedControllerStickPosition(playerNum)
    return ffi.new("StickPos")
end

-- Matches inputConfig1/2 (classes.lua), which say keyboard. ffi.string takes a Lua string.
function dll.LunaLuaGetSelectedControllerName(playerNum)
    return "Keyboard"
end


----------------------
-- ffi_graphics.lua --
----------------------
-- Drawing is Phase 4. Until then: answers to the queries, nothing drawn.

-- Memory ffi_graphics fills with vertex data for the GL draw calls, which free
-- it once drawn (malloc in LunaLua). Here it's LuaJIT memory, kept alive
-- until the end of the next frame, since nothing draws it yet.
local allocations, lastFrameAllocations = {}, {}
function dll.LunaLuaAlloc(size)
    local buf = ffi.new("uint8_t[?]", size)
    allocations[#allocations + 1] = buf
    return buf
end
core.onFrameEnd(function()
    lastFrameAllocations = allocations
    allocations = {}
end)

function dll.FFI_GraphicsIsSoftwareGL()
    return false
end

-- OpenGL limits scripts can check before using shader features. Zeroed
-- (empty strings, no features) except the version, so feature checks fail
-- safely. Kept in an upvalue so the struct outlives the call; created on first
-- use because ffi_graphics.lua declares the type after this file loads.
local glConstants
function dll.FFI_GraphicsGetConstants()
    if not glConstants then
        glConstants = ffi.new("GLConstants")
        glConstants.iMAJOR_VERSION = 2
        glConstants.iMINOR_VERSION = 1
        glConstants.iMAX_TEXTURE_SIZE = 4096
    end
    return glConstants
end

-- Images: a LunaImageRef* is a numbered handle into this table (0 would read
-- as NULL, so numbering starts at 1). Phase 4 gives them textures.
local images = {}
local nextImage = 1
core.images = images

-- Image sizes by path. Like LunaLua's image cache, so a script that loads the
-- same file every frame (SMATRS's dialogue fonts do) doesn't reread it each time.
local imageSizes = {}

local function newImage(path, sizeOut)
    local size = imageSizes[path]
    if size == nil then
        size = xtech_misc_imageSize(path)
        if not size then
            return nil
        end
        imageSizes[path] = size
    end
    local id = nextImage
    nextImage = nextImage + 1
    images[id] = { path = path, w = size.w, h = size.h }
    if sizeOut ~= nil then
        sizeOut[0] = size.w
        sizeOut[1] = size.h
    end
    return ffi.cast("LunaImageRef*", id)
end

function dll.FFI_ImageLoad(filename, sizeOut)
    -- Like LunaLua: relative paths are in the level's folder
    filename = core.normalize(filename)
    if string.sub(filename, 1, 1) ~= "/" then
        filename = core.customPath .. filename
    end
    local path = core.resolve(filename)
    -- NULL, not nil: ffi_graphics passes the result to ffi.gc either way
    return path and newImage(path, sizeOut) or ffi.cast("LunaImageRef*", nil)
end

local function imageAt(ref)
    return images[tonumber(ffi.cast("uintptr_t", ref))]
end

function dll.FFI_ImageFree(img)
    images[tonumber(ffi.cast("uintptr_t", img))] = nil
end

-- LunaLua returns the pixel buffer's address as a uint32 and ffi_graphics casts
-- it to uint32_t*; returning the pointer itself works the same here (and on 64-bit).
-- Decoded on first use and kept with the image.
function dll.FFI_ImageGetDataPtr(img)
    local image = imageAt(img)
    if not image then
        return nil
    end
    if not image.pixels then
        local data = xtech_misc_imagePixels(image.path)
        if #data ~= image.w * image.h * 4 then
            core.warn("couldn't read pixels of " .. image.path)
            return nil
        end
        image.pixels = ffi.new("uint32_t[?]", image.w * image.h)
        ffi.copy(image.pixels, data, #data)
    end
    return image.pixels
end

-- Graphics.sprites: images by sprite name. Built-in names ("npc-1") live in
-- graphics/<prefix>/, extra ones registered by libraries in graphics/<folder>/;
-- the level and episode folders override both (ImageLoader::GetByName).
-- Overrides set by scripts are kept here; Phase 4 makes the engine draw them.
local extraSprites = {}
local spriteOverrides = {}

function dll.FFI_RegisterExtraSprite(folderName, name)
    if extraSprites[name] == nil then
        extraSprites[name] = folderName
    end
end

function dll.FFI_SetSpriteOverride(name, img)
    core.missingAPI("sprite overrides (kept, not drawn)")
    spriteOverrides[name] = img ~= nil and imageAt(img) or nil
end

function dll.FFI_GetSpriteOverride(name, sizeOut)
    local override = spriteOverrides[name]
    if override then
        return newImage(override.path, sizeOut)
    end
    local file = name .. ".png"
    local path
    local folder = extraSprites[name]
    if folder then
        path = core.resolve(core.customPath .. file) or core.resolve(core.episodePath .. file)
            or core.resolve(core.smbxPath .. "/graphics/" .. folder .. "/" .. file)
    else
        path = Misc.resolveGraphicsFile(file)
        path = path and core.normalize(path)
    end
    -- NULL for IDs with no graphic (callers walk every ID up to the maximum)
    return path and newImage(path, sizeOut) or ffi.cast("LunaImageRef*", nil)
end

local nextHandle = 1
local function newHandle(ctype)
    local h = ffi.cast(ctype, nextHandle)
    nextHandle = nextHandle + 1
    return h
end

local function handleKey(h)
    return tonumber(ffi.cast("uintptr_t", h))
end

-- Shaders: nothing is compiled yet (Phase 4), but scripts such as darkness.lua
-- need a compiled shader and its variable list right away, and an error there
-- stops every later onStart handler. So compiling always succeeds, and the
-- uniforms and attributes come from the declarations in the source, shaped
-- like GL's reflection: arrays as "name[0]" with the element count.
local shaders = {}

-- GLSL type -> GL type enum
local glslTypes = {
    float = 0x1406, vec2 = 0x8B50, vec3 = 0x8B51, vec4 = 0x8B52,
    int = 0x1404, ivec2 = 0x8B53, ivec3 = 0x8B54, ivec4 = 0x8B55,
    uint = 0x1405, uvec2 = 0x8DC6, uvec3 = 0x8DC7, uvec4 = 0x8DC8,
    bool = 0x8B56, bvec2 = 0x8B57, bvec3 = 0x8B58, bvec4 = 0x8B59,
    mat2 = 0x8B5A, mat3 = 0x8B5B, mat4 = 0x8B5C,
    mat2x2 = 0x8B5A, mat3x3 = 0x8B5B, mat4x4 = 0x8B5C,
    mat2x3 = 0x8B65, mat2x4 = 0x8B66, mat3x2 = 0x8B67,
    mat3x4 = 0x8B68, mat4x2 = 0x8B69, mat4x3 = 0x8B6A,
    sampler1D = 0x8B5D, sampler2D = 0x8B5E, sampler3D = 0x8B5F, samplerCube = 0x8B60,
    sampler1DShadow = 0x8B61, sampler2DShadow = 0x8B62, sampler2DRect = 0x8B63,
}
local qualifiers = { lowp = true, mediump = true, highp = true, const = true,
                     flat = true, smooth = true, noperspective = true }

-- "[8]" -> 8; sizes are numbers once SMBX2's preprocessor has run, but allow
-- simple arithmetic
local function arraySize(spec)
    local expr = string.match(spec, "^%[(.*)%]$")
    if expr == nil or expr == "" then
        return nil
    end
    if string.find(expr, "^[%d%s%+%-%*/%(%)]+$") then
        local f = loadstring("return " .. expr)
        local ok, n = pcall(f)
        if ok and type(n) == "number" then
            return math.floor(n)
        end
    end
    return 1
end

-- Drops initializers ("= vec2(1.0, 2.0)") while splitting declarators at commas
local function declarators(text)
    local list, depth, start, skipping = {}, 0, 1, false
    for i = 1, #text + 1 do
        local c = string.sub(text, i, i)
        if c == "(" then
            depth = depth + 1
        elseif c == ")" then
            depth = depth - 1
        elseif (c == "," and depth == 0) or c == "" then
            list[#list + 1] = string.match(string.sub(text, start, i - 1), "^%s*([^=]-)%s*=") or
                              string.match(string.sub(text, start, i - 1), "^%s*(.-)%s*$")
            start = i + 1
        end
    end
    return list
end

local function parseVariables(source, keywords, varInfoType, out)
    source = string.gsub(source, "/%*.-%*/", " ")
    source = string.gsub(source, "//[^\n]*", "")
    source = string.gsub("\n" .. source, "\n%s*#[^\n]*", "\n")
    for statement in string.gmatch(source, "[^;{}]+") do
        local words = string.match(statement, "^%s*(.-)%s*$")
        local first, rest = string.match(words, "^(%a+)%s+(.*)$")
        if first and keywords[first] then
            -- skip precision/interpolation qualifiers, then the type
            local typeName
            repeat
                typeName, rest = string.match(rest, "^([%w_]+)%s*(.*)$")
            until typeName == nil or not qualifiers[typeName]
            local glType = typeName and glslTypes[typeName]
            if glType then
                -- GLSL 1.20 also allows the array size on the type: "mat3x4 [8] lights"
                local typeArray, after = string.match(rest, "^(%b[])%s*(.*)$")
                if typeArray then
                    rest = after
                end
                for _, decl in ipairs(declarators(rest)) do
                    local name, spec = string.match(decl, "^([%a_][%w_]*)%s*(%b[])$")
                    name = name or string.match(decl, "^([%a_][%w_]*)$")
                    if name then
                        local count = arraySize(spec or typeArray or "")
                        local entry = {
                            varInfoType = varInfoType,
                            id = #out,
                            arrayCount = count or 1,
                            type = glType,
                            name = count and (name .. "[0]") or name,
                            rawName = name,
                            arrayDepth = count and 1 or 0,
                        }
                        out[#out + 1] = entry
                    end
                end
            end
        end
    end
    return out
end

function dll.FFI_ShaderFromStrings(vertexSource, fragmentSource)
    core.missingAPI("shaders (variables read from source; nothing is drawn)")
    local h = newHandle("FFI_ShaderObjRef*")
    vertexSource, fragmentSource = vertexSource or "", fragmentSource or ""
    local uniforms = parseVariables(vertexSource, { uniform = true }, 1, {})
    local seen = {}
    for _, u in ipairs(uniforms) do
        seen[u.rawName] = true
    end
    for _, u in ipairs(parseVariables(fragmentSource, { uniform = true }, 1, {})) do
        if not seen[u.rawName] then
            u.id = #uniforms
            uniforms[#uniforms + 1] = u
        end
    end
    shaders[handleKey(h)] = {
        uniforms = uniforms,
        attributes = parseVariables(vertexSource, { attribute = true, ["in"] = true }, 0, {}),
    }
    return h
end

function dll.FFI_ShaderFree(obj)
    shaders[handleKey(obj)] = nil
end

function dll.FFI_ShaderCompile(obj)
    return shaders[handleKey(obj)] ~= nil
end

function dll.FFI_ShaderError(obj)
    return nil
end

-- ffi_graphics reads these with ffi.string, which takes a Lua string as well
function dll.FFI_GetUniformInfo(obj, idx)
    local s = shaders[handleKey(obj)]
    return s and s.uniforms[tonumber(idx) + 1]
end

function dll.FFI_GetAttributeInfo(obj, idx)
    local s = shaders[handleKey(obj)]
    return s and s.attributes[tonumber(idx) + 1]
end

-- Capture buffers (render targets): handles; capturing and drawing is Phase 4
function dll.FFI_CaptureBuffer(w, h, nonskippable)
    return newHandle("CaptureBufferRef*")
end

function dll.FFI_CaptureBufferFree(buf)
end

function dll.FFI_GraphicsGetMainFramebufferSize()
    return ffi.new("FBSize", 800, 600)
end


-------------------
-- ffi_layer.lua --
-------------------

function dll.LunaLuaShowLayer(layerIndex, noSmoke)
    xtech_layer_show(layerIndex, noSmoke and true or false)
end

function dll.LunaLuaHideLayer(layerIndex, noSmoke)
    xtech_layer_hide(layerIndex, noSmoke and true or false)
end


--------------------------
-- ffi_customparams.lua --
--------------------------
-- Per-object settings from the level file, merged over the config pack's
-- defaults (extrasettings.lua). ffi_customparams only indexes these arrays and
-- hands entries to ffi.string, so Lua tables of strings stand in for LunaLua's
-- const char**, with index 0 the first object like theirs. nil = no settings.

local extraSettings = core.extraSettings

-- kind is "npc", "bgo" or "block"; the engine counts from 1
local function objectParams(kind)
    local t = xtech_misc_customParams(kind)
    local ret = {}
    for i, id in ipairs(t.ids) do
        ret[i - 1] = extraSettings.object(kind, id, t.params[i])
    end
    return ret
end

function dll.LunaLuaGetLevelCustomParams()
    return extraSettings.simple("level", xtech_misc_customParams("level"))
end

function dll.LunaLuaGetSectionCustomParams()
    local t = xtech_misc_customParams("section")
    local ret = {}
    for i = 0, 20 do
        ret[i] = extraSettings.simple("section", t[i])
    end
    return ret
end

function dll.LunaLuaGetNpcCustomParams()
    return objectParams("npc")
end

function dll.LunaLuaGetBgoCustomParams()
    return objectParams("bgo")
end

function dll.LunaLuaGetBlockCustomParams()
    return objectParams("block")
end

function dll.LunaLuaGetDefaultLevelCustomParams()
    return extraSettings.simple("level", "{}")
end

function dll.LunaLuaGetDefaultSectionCustomParams()
    return extraSettings.simple("section", "{}")
end

function dll.LunaLuaGetDefaultNpcCustomParams(id)
    return extraSettings.object("npc", id, "{}")
end

function dll.LunaLuaGetDefaultBgoCustomParams(id)
    return extraSettings.object("bgo", id, "{}")
end

function dll.LunaLuaGetDefaultBlockCustomParams(id)
    return extraSettings.object("block", id, "{}")
end


------------------------------------------
-- ffi_player.lua, ffi_npc.lua, ffi_block.lua --
------------------------------------------
-- LunaLua keeps extra per-object fields beside SMBX's own memory. The struct
-- layouts are LunaLua's (LuaProxyFFI.cpp); the arrays are ours, indexed like
-- the engine's object arrays. Players reach index 1000+ for character templates.

-- Upper bounds from src/global_constants.h (maxNPCs, maxBlocks)
local MAX_NPCS = 5000
local MAX_BLOCKS = 20000
local PLAYER_SLOTS = 1100

function dll.LunaLuaGetPlayerExtendedFieldsStruct()
    return [[
typedef struct ExtendedPlayerFields_
{
    bool noblockcollision;
    bool nonpcinteraction;
    bool noplayerinteraction;
    unsigned int collisionGroup;
    int slidingTimeSinceOnSlope;
} ExtendedPlayerFields;]]
end

function dll.LunaLuaGetNPCextendedFieldsStruct()
    return [[
typedef struct ExtendedNPCFields_
{
    bool noblockcollision;
    bool nonpccollision;
    short fullyInsideSection;
    unsigned int collisionGroup;
} ExtendedNPCFields;]]
end

function dll.LunaLuaGetBlockExtendedFieldsStruct()
    return [[
typedef struct ExtendedBlockFields_
{
    double layerSpeedX;
    double layerSpeedY;
    double extraSpeedX;
    double extraSpeedY;
    unsigned int collisionGroup;
} ExtendedBlockFields;]]
end

-- Each array lives as long as this Lua state; upvalues keep them from being collected
local playerExtended, npcExtended, blockExtended
function dll.LunaLuaGetPlayerExtendedFieldsArray()
    playerExtended = playerExtended or ffi.new("ExtendedPlayerFields[?]", PLAYER_SLOTS)
    return playerExtended
end
function dll.LunaLuaGetNPCExtendedFieldsArray()
    npcExtended = npcExtended or ffi.new("ExtendedNPCFields[?]", MAX_NPCS + 1)
    return npcExtended
end
function dll.LunaLuaGetBlockExtendedFieldsArray()
    blockExtended = blockExtended or ffi.new("ExtendedBlockFields[?]", MAX_BLOCKS + 1)
    return blockExtended
end


-----------------------------------------------------------
-- Collision groups (ffi_misc.lua and the ffi_* objects) --
-----------------------------------------------------------
-- LunaLua's CollisionMatrix (Misc/CollisionMatrix.cpp), with the group-name
-- layer SMBX2 now expects on top. Objects store a group index in their extended
-- fields; index 0 is the unnamed default group. By default two objects collide
-- unless they share a named group; Misc.groupsCollide flips single pairs.
-- An index is freed once no object and no flipped pair refers to it.
-- TODO: the engine's own collisions don't consult this yet; only scripts do.

local groupNames = { [0] = "" } -- index -> name
local groupIndices = { [""] = 0 } -- name -> index
local groupRefs = {}            -- index -> objects + flipped pairs using it
local groupStrings = {}         -- index -> the FFIString handed to scripts
local freeIndices = {}
local nextIndex = 1
local flipped = {}              -- min * PAIR_KEY + max -> true where not default
local flippedCount = 0
local PAIR_KEY = 2^24

local function groupName(name)
    if name == nil then
        return ""
    end
    return tostring(name)
end

-- The index for a name, made on first use (with no references yet)
local function indexFor(name)
    local idx = groupIndices[name]
    if idx then
        return idx
    end
    idx = table.remove(freeIndices) or nextIndex
    if idx == nextIndex then
        nextIndex = nextIndex + 1
    end
    groupNames[idx] = name
    groupIndices[name] = idx
    groupRefs[idx] = 0
    groupStrings[idx] = nil
    return idx
end

local function releaseIfUnused(idx)
    if idx ~= 0 and groupRefs[idx] == 0 then
        groupIndices[groupNames[idx]] = nil
        groupNames[idx] = nil
        groupRefs[idx] = nil
        groupStrings[idx] = nil
        freeIndices[#freeIndices + 1] = idx
    end
end

local function addRef(idx, n)
    if idx ~= 0 and groupRefs[idx] then
        groupRefs[idx] = groupRefs[idx] + n
        releaseIfUnused(idx)
    end
end

local function indicesCollide(i, j)
    if i > j then
        i, j = j, i
    end
    local collide = i == 0 or i ~= j
    if flippedCount > 0 and flipped[i * PAIR_KEY + j] then
        return not collide
    end
    return collide
end

function dll.LunaLuaCollisionMatrixGetGroupIndicesCollide(i, j)
    return indicesCollide(i, j)
end

-- A name nothing uses yet has no flipped pairs and differs from every used group
function dll.LunaLuaCollisionMatrixGetGroupIndexCollidesWithGroup(i, name)
    local j = groupIndices[groupName(name)]
    if j == nil then
        return true
    end
    return indicesCollide(i, j)
end

function dll.LunaLuaCollisionMatrixGetGroupsCollide(a, b)
    a, b = groupName(a), groupName(b)
    local i, j = groupIndices[a], groupIndices[b]
    if i == nil or j == nil then
        return a == "" or b == "" or a ~= b
    end
    return indicesCollide(i, j)
end

function dll.LunaLuaCollisionMatrixSetGroupsCollide(a, b, collide)
    local i, j = indexFor(groupName(a)), indexFor(groupName(b))
    if i > j then
        i, j = j, i
    end
    local key = i * PAIR_KEY + j
    local wantFlip = collide ~= (i == 0 or i ~= j)
    if wantFlip and not flipped[key] then
        flipped[key] = true
        flippedCount = flippedCount + 1
        addRef(i, 1)
        addRef(j, 1)
    elseif not wantFlip and flipped[key] then
        flipped[key] = nil
        flippedCount = flippedCount - 1
        addRef(i, -1)
        addRef(j, -1)
    else
        releaseIfUnused(i)
        releaseIfUnused(j)
    end
end

-- Moves one object from its old group to a new one and returns the new index
function dll.LunaLuaCollisionMatrixAssignGroup(previous, name)
    local idx = indexFor(groupName(name))
    addRef(idx, 1)
    addRef(previous, -1)
    return idx
end

-- Scripts read it as ffi.string(s.buf, s.size), which takes a Lua string as buf
function dll.LunaLuaCollisionMatrixGetGroupFromIndex(idx)
    local s = groupStrings[idx]
    if s == nil then
        local name = groupNames[idx]
        if name == nil then
            return { buf = "", size = 0 }
        end
        s = { buf = name, size = #name }
        groupStrings[idx] = s
    end
    return s
end

-- LunaLua's gRawKeymap[4]: this frame's keys for players 1-2, then last frame's.
-- TODO: fill from the engine's controls each frame.
local rawKeymap
function dll.LunaLuaGetRawKeymapArray()
    rawKeymap = rawKeymap or ffi.new("LunaLuaKeyMap[4]")
    return rawKeymap
end

-- Player(1000 + id): the stored state of a character not in play. These live in
-- the engine's mem() emulation (xtech_mem_get), 0 for IDs it has no room for.
function dll.LunaLuaGetTemplateAddressForCharacter(id)
    return xtech_mem_templateAddress(id)
end

-- Zero-terminated. TheXTech has SMBX 1.3's five characters.
local characterIds
function dll.LunaLuaGetValidCharacterIDArray()
    characterIds = characterIds or ffi.new("short[6]", 1, 2, 3, 4, 5, 0)
    return characterIds
end


return setmetatable(dll, {
    __index = function(t, name)
        local accessor = defineAccessor(name)
        if accessor then
            rawset(t, name, accessor)
            return accessor
        end
        local function notYet()
            core.missingAPI("LunaDll." .. tostring(name) .. "()")
        end
        rawset(t, name, notYet)
        return notYet
    end,
})
