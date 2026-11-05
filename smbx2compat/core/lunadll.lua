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
-- Drawing goes to the engine's draw queue (script/src/xtech_lua_draw.cpp), which
-- draws each script draw where its render priority falls among the engine's
-- objects. Shaders are translated by glsl.lua and compiled by the engine;
-- capture buffers are the engine's render targets. Clip planes aren't drawn yet.

-- The engine's C functions, called through the FFI so LuaJIT can compile the
-- calls. Cast on first call: ffi_graphics.lua declares their argument types
-- after this file loads.
local function engineFunction(name, ctype, address)
    dll[name] = function(...)
        local f = ffi.cast(ctype, address)
        dll[name] = f
        return f(...)
    end
end

-- Arrays ffi_graphics fills for FFI_GLDraw, which frees them (malloc, as in LunaLua)
dll.LunaLuaAlloc = ffi.cast("void *(*)(size_t)", __xtech_ffiAlloc)
engineFunction("FFI_GLDraw", "void (*)(const FFI_GL_Draw_Cmd*)", __xtech_ffiGLDraw)
engineFunction("FFI_ImageDraw",
    "void (*)(LunaImageRef*, double, double, double, double, double, double, double, float, bool)",
    __xtech_ffiImageDraw)
engineFunction("FFI_TextDraw", "void (*)(const char*, int, int, int, double, bool)", __xtech_ffiTextDraw)

-- SMBX2 libraries that draw backgrounds (paralx2), BGOs (betterbgo), sizable
-- blocks (sizable) or the HUD themselves switch the engine's drawing off.
-- Numbers: XTechLuaVanillaDraw in script/include/xtech_lua_draw.h.
function dll.LunaLuaSetBackgroundRenderFlag(val)
    xtech_gfx_setVanilla(0, val)
end

function dll.LunaLuaSetBGORenderFlag(val)
    xtech_gfx_setVanilla(1, val)
end

function dll.LunaLuaSetSizableRenderFlag(val)
    xtech_gfx_setVanilla(2, val)
end

function dll.FFI_GraphicsActivateHud(activate)
    xtech_gfx_setVanilla(3, activate)
end

function dll.FFI_GraphicsIsHudActivated()
    return xtech_gfx_getVanilla(3)
end

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

-- Images: a LunaImageRef* is the engine's number for a loaded picture (from 1,
-- so never NULL). The engine shares one picture among loads of the same file,
-- like LunaLua's image cache (SMATRS loads its dialogue fonts every frame).
-- This table keeps each one's path, and its pixels once a script reads them.
local images = {}

local function newImage(path, sizeOut)
    local id, w, h = xtech_gfx_imageLoad(path)
    if not id then
        return nil
    end
    images[id] = { path = path, w = w, h = h }
    if sizeOut ~= nil then
        sizeOut[0] = w
        sizeOut[1] = h
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
    local id = tonumber(ffi.cast("uintptr_t", img))
    images[id] = nil
    xtech_gfx_imageFree(id)
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
-- Overrides set by scripts are kept here, and the engine draws its own sprites
-- ("block-760" -> category "block", ID 760) with them (SpriteOverride_Set).
local extraSprites = {}
local spriteOverrides = {}
local engineSprites = {
    block = true, npc = true, effect = true, background = true, background2 = true,
    yoshib = true, yoshit = true, mario = true, luigi = true, peach = true, toad = true, link = true,
}

function dll.FFI_RegisterExtraSprite(folderName, name)
    if extraSprites[name] == nil then
        extraSprites[name] = folderName
    end
end

function dll.FFI_SetSpriteOverride(name, img)
    local image = img ~= nil and imageAt(img) or nil
    spriteOverrides[name] = image

    if extraSprites[name] then
        return -- drawn by scripts only, which ask FFI_GetSpriteOverride
    end
    local category, id = string.match(name, "^(.-)%-(%d+)$")
    if not engineSprites[category] then
        if image then
            core.missingAPI("sprite overrides of " .. string.match(name, "^%a*") .. " (kept, not drawn)")
        end
    elseif not xtech_gfx_setSpriteOverride(category, tonumber(id), image and image.path) and image then
        core.missingAPI("sprite overrides past the engine's IDs (kept, not drawn)")
    end
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

-- Shaders: compiled by the engine from glsl.lua's translation; their variable
-- lists come from GL, as in LunaLua. Scripts such as darkness.lua need a
-- compiled shader and its variables right away, and an error there stops every
-- later onStart handler, so a shader that doesn't compile here (a gap in the
-- translation, or no shader support) still reports success, with the
-- variables read from its source, and draws without it.
local shaders = {}
local glslHeader = xtech_gfx_shaderHeader()

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

local function sourceVariables(vertexSource, fragmentSource)
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
    return uniforms, parseVariables(vertexSource, { attribute = true, ["in"] = true }, 0, {})
end

-- GL's active variables, shaped like LunaLua's GLShaderVariableInfo
local function engineVariables(key, attributes)
    local out = {}
    for _, v in ipairs(xtech_gfx_shaderVariables(key, attributes) or {}) do
        local name = core.glsl.scriptName(v.name)
        if name then
            local _, depth = string.gsub(name, "%[%d*%]", "")
            out[#out + 1] = {
                varInfoType = attributes and 0 or 1,
                id = v.location,
                arrayCount = v.size,
                type = v.type,
                name = name,
                rawName = string.match(name, "^[^%[]*"),
                arrayDepth = depth,
            }
        end
    end
    return out
end

-- First lines of a shader, to say which one failed
local function shaderLabel(source)
    local lines = {}
    for line in string.gmatch(source, "[^\n]+") do
        line = string.match(line, "^%s*(.-)%s*$")
        if line ~= "" and not string.find(line, "^#") then
            lines[#lines + 1] = line
            if #lines == 3 then
                break
            end
        end
    end
    return table.concat(lines, " | ")
end

function dll.FFI_ShaderFromStrings(vertexSource, fragmentSource)
    local h = newHandle("FFI_ShaderObjRef*")
    shaders[handleKey(h)] = { vertex = vertexSource or "", fragment = fragmentSource or "" }
    return h
end

function dll.FFI_ShaderFree(obj)
    local key = handleKey(obj)
    shaders[key] = nil
    xtech_gfx_shaderFree(key)
end

local function compileShader(key)
    local s = shaders[key]

    local ok, err = false, "this renderer has no shaders"
    if glslHeader then
        local vs, fs = core.glsl.translate(s.vertex, s.fragment, glslHeader)
        ok, err = xtech_gfx_shaderCompile(key, vs, fs)
    end

    if ok then
        s.uniforms = engineVariables(key, false)
        s.attributes = engineVariables(key, true)
    else
        core.warn("shader didn't compile here (drawn without it): " ..
                  shaderLabel(s.fragment) .. "\n" .. tostring(err))
        s.uniforms, s.attributes = sourceVariables(s.vertex, s.fragment)
    end
    return ok
end

function dll.FFI_ShaderCompile(obj)
    local key = handleKey(obj)
    local s = shaders[key]
    if s == nil then
        return false
    end
    if not s.uniforms then
        compileShader(key)
    end
    return true
end

function dll.FFI_ShaderError(obj)
    return nil
end

-- ffi_graphics reads these with ffi.string, which takes a Lua string as well
function dll.FFI_GetUniformInfo(obj, idx)
    local s = shaders[handleKey(obj)]
    return s and s.uniforms and s.uniforms[tonumber(idx) + 1]
end

function dll.FFI_GetAttributeInfo(obj, idx)
    local s = shaders[handleKey(obj)]
    return s and s.attributes and s.attributes[tonumber(idx) + 1]
end

-- Draws without a shader that the engine's own queue can't make (into or from
-- capture buffers, lines) use SMBX2's standard shader. A plain handle number:
-- FFI_ShaderObjRef isn't declared yet while this file loads.
do
    local key = nextHandle
    nextHandle = nextHandle + 1
    shaders[key] = {
        vertex = core.readFile(core.smbxPath .. "/scripts/shaders/standard.vert") or "",
        fragment = core.readFile(core.smbxPath .. "/scripts/shaders/standard.frag") or "",
    }
    if compileShader(key) then
        xtech_gfx_setStandardShader(key)
    end
end

-- Capture buffers: the engine's render targets
function dll.FFI_CaptureBuffer(w, h, nonskippable)
    local handle = newHandle("CaptureBufferRef*")
    if not xtech_gfx_bufferCreate(handleKey(handle), tonumber(w), tonumber(h)) then
        core.missingAPI("capture buffers (this renderer has none; draws with them are skipped)")
    end
    return handle
end

function dll.FFI_CaptureBufferFree(buf)
    xtech_gfx_bufferFree(handleKey(buf))
end

function dll.FFI_CaptureBufferCaptureAt(buf, priority)
    xtech_gfx_bufferCapture(handleKey(buf), tonumber(priority))
end

function dll.FFI_CaptureBufferClear(buf, priority)
    xtech_gfx_bufferClear(handleKey(buf), tonumber(priority))
end

-- The engine's screen size (SMATRS asks for 960x540)
function dll.FFI_GraphicsSetMainFramebufferSize(w, h)
    xtech_gfx_setFramebufferSize(w, h)
end

function dll.FFI_GraphicsGetMainFramebufferSize()
    return ffi.new("FBSize", xtech_gfx_getFramebufferSize())
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
