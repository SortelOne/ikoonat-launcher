--[[
glsl.lua: SMBX2's shaders for the engine's OpenGL context.

SMBX2 writes GLSL 1.20 for LunaLua's OpenGL 2.1 compatibility context and uses
its fixed-function built-ins (gl_Vertex, gl_Color, gl_TexCoord[0],
gl_ModelViewProjectionMatrix, ...). The engine runs an OpenGL 3.3+ core or
OpenGL ES context, whose GLSL has none of them, so translate() swaps them for
the variables the engine's script draws provide (render_gl_script.cpp):

  gl_Vertex, gl_MultiTexCoord0, gl_Color (vertex)  -> xt_Vertex, xt_MultiTexCoord0, xt_Color
  gl_FrontColor (vertex), gl_Color (fragment)      -> xt_FrontColor
  gl_TexCoord[n]                                   -> xt_TexCoordn
  gl_ModelViewProjectionMatrix, ...ModelView..., ...Projection..., gl_TextureMatrix[0]
                                                   -> xt_MVP, xt_MV, xt_P, xt_TexMatrix0
  gl_FragColor                                     -> xt_FragColor (GLSL 3.30 / ES 3.00)

and, for GLSL 3.30 / ES 3.00, attribute/varying -> in/out and texture2D ->
texture. Names that later GLSL reserves (SMATRS has uniforms named
textureSize) become xt_r_<name>; lunadll.lua reports them by their own name.

Clip planes (glDraw's clipPlanes; LunaLua uses fixed-function glClipPlane,
which neither GL 3.3 core nor ES has): each stage's main() becomes xt_main(),
called by a main() of ours. The vertex one passes the position in the view
(xt_MV * xt_Vertex, or gl_ClipVertex if the shader sets it, as fixed-function
GL does) to the fragment one, which discards pixels behind any of the
xt_ClipPlanes planes in xt_ClipPlane (set by the engine for every draw).

OpenGL ES differs from the GLSL 1.20 SMBX2 writes in two ways its shaders rely on:
- No uniform initializers ("uniform vec2 frame = vec2(1.0, 1.0);"): for ES they
  are taken out, and translate() returns their values ("name=v,v,...\n" lines),
  which the engine sets after linking (render_gl_script.cpp). A value that isn't
  numbers (an expression) is dropped; translate() names those too.
- No implicit int -> float conversion (max(x, 0), x * 2): the engine's header
  turns on GL_EXT_shader_implicit_conversions where the GPU has it (ESSL 3.10);
  without it such shaders still don't compile. Adreno's doesn't cover built-in
  calls that mix ints and floats (max(1, x)), so whole numbers passed straight
  to GLSL 1.20's float-only built-ins get a ".0" (floatArguments).
Built-ins besides the ones above aren't handled. A shader that doesn't compile
is drawn without.
]]

local glsl = {}

-- Built-in functions and keywords of GLSL 1.30+ that 1.20 code may use as names
local laterNames = {
    "texture", "textureSize", "textureLod", "textureProj", "textureOffset",
    "textureGrad", "texelFetch", "sample", "smooth", "flat", "noperspective",
    "centroid", "layout", "patch", "subroutine", "round", "roundEven", "trunc",
    "sinh", "cosh", "tanh", "asinh", "acosh", "atanh", "isnan", "isinf", "modf",
    "determinant", "inverse", "uint", "uvec2", "uvec3", "uvec4",
}

local function word(name)
    return "%f[%w_]" .. name .. "%f[^%w_]"
end

-- Replaces name as a whole word
local function swap(source, name, by)
    return (string.gsub(source, word(name), by))
end

-- Common to both stages; returns the source and the highest gl_TexCoord index
local function translateCommon(source, modern, version)
    -- the header supplies #version
    source = string.gsub(source, "#[ \t]*version[^\n]*", "")

    if modern and version < 130 then
        for _, name in ipairs(laterNames) do
            source = swap(source, name, "xt_r_" .. name)
        end
    end

    local maxCoord = -1
    source = string.gsub(source, "gl_TexCoord%s*%[%s*(%d+)%s*%]", function(n)
        maxCoord = math.max(maxCoord, tonumber(n))
        return "xt_TexCoord" .. n
    end)

    source = swap(source, "gl_ModelViewProjectionMatrix", "xt_MVP")
    source = swap(source, "gl_ModelViewMatrix", "xt_MV")
    source = swap(source, "gl_ProjectionMatrix", "xt_P")
    source = string.gsub(source, "gl_TextureMatrix%s*%[%s*0%s*%]", "xt_TexMatrix0")

    if modern then
        source = string.gsub(source, word("texture2DProj") .. "%s*%(", "textureProj(")
        source = string.gsub(source, word("texture2DLod") .. "%s*%(", "textureLod(")
        source = string.gsub(source, word("texture2D") .. "%s*%(", "texture(")
    end

    return source, maxCoord
end

local function sourceVersion(source)
    return tonumber(string.match(source, "#[ \t]*version[ \t]+(%d+)")) or 110
end

-- GLSL 1.20's built-in functions, all for floats only
local floatFunctions = {}
for name in string.gmatch("radians degrees sin cos tan asin acos atan pow exp log exp2 log2 " ..
                          "sqrt inversesqrt abs sign floor ceil fract mod min max clamp mix " ..
                          "step smoothstep length distance dot cross normalize faceforward " ..
                          "reflect refract", "%S+") do
    floatFunctions[name] = true
end

-- Splits a call's arguments at its top-level commas
local function splitArguments(text)
    local list, depth, start = {}, 0, 1
    for i = 1, #text do
        local c = string.sub(text, i, i)
        if c == "(" or c == "[" then
            depth = depth + 1
        elseif c == ")" or c == "]" then
            depth = depth - 1
        elseif c == "," and depth == 0 then
            list[#list + 1] = string.sub(text, start, i - 1)
            start = i + 1
        end
    end
    list[#list + 1] = string.sub(text, start)
    return list
end

-- The ones GLSL 1.30 and later also have for ints
local intToo = { abs = true, sign = true, min = true, max = true, clamp = true }

local function wholeNumber(arg)
    return string.match(arg, "^(%s*[-+]?%s*%d+)(%s*)$")
end

-- A first argument that is certainly a float: it has a decimal number in it
-- and isn't converted to an int type
local function surelyFloat(arg)
    if string.find(arg, "^%s*[iu]vec%d") or string.find(arg, "^%s*u?int%s*%(") then
        return false
    end
    return string.find(arg, "%f[%w_]%d+%.") ~= nil
end

-- A whole number passed straight to one of those ("max(1, x)", "clamp(n, 0, 1)")
-- is a float in GLSL 1.20. ESSL also has int versions of some, and Adreno's
-- implicit conversions don't cover calls that mix the two, so for ES the
-- number gets its ".0". GLSL 1.30+ sources (modern) have the int versions
-- too: there the ".0" goes only to the float-only functions, and min, max
-- and clamp with a whole number either get it when their first argument is
-- surely a float, or else go through xt_<name>L, overloaded for float and
-- int types (limitHelpers), so the compiler picks by type. On the Odin,
-- lib3d's alpha cutoff, clamp(sign(a - 0.5),0,1), came out ~0 and threw away
-- every pixel of SMATRS's map terrain (the only ALPHA_CUTOFF material).
-- used: names of the helpers needed.
local function floatArguments(source, modern, used)
    return (string.gsub(source, "(%f[%w_][%a_][%w_]*)(%s*)(%b())", function(name, space, args)
        local inner = floatArguments(string.sub(args, 2, -2), modern, used)
        if floatFunctions[name] then
            local list = splitArguments(inner)
            local limits = modern and intToo[name]
            local hasNumber = false
            for i, arg in ipairs(list) do
                if wholeNumber(arg) then
                    hasNumber = true
                end
            end
            if hasNumber and limits then
                if name == "abs" or name == "sign" then
                    return name .. space .. "(" .. inner .. ")"
                end
                local first
                for _, arg in ipairs(list) do
                    if not wholeNumber(arg) then
                        first = arg
                        break
                    end
                end
                if not (first and surelyFloat(first)) then
                    used[name] = true
                    return "xt_" .. name .. "L" .. space .. "(" .. inner .. ")"
                end
            end
            if hasNumber then
                for i, arg in ipairs(list) do
                    local number, after = wholeNumber(arg)
                    if number then
                        list[i] = number .. ".0" .. after
                    end
                end
                inner = table.concat(list, ",")
            end
        end
        return name .. space .. "(" .. inner .. ")"
    end))
end

-- xt_clampL/xt_minL/xt_maxL for floatArguments
local function limitHelpers(used)
    local out = {}
    local floats = { "float", "vec2", "vec3", "vec4" }
    local ints = { "int", "ivec2", "ivec3", "ivec4" }
    for _, name in ipairs({ "clamp", "min", "max" }) do
        if used[name] then
            for k, t in ipairs(floats) do
                for _, pair in ipairs({ { t, "float" }, { ints[k], "int" } }) do
                    local v, s = pair[1], pair[2]
                    if name == "clamp" then
                        out[#out + 1] = v .. " xt_clampL(" .. v .. " x, " .. s .. " a, " .. s .. " b) { return clamp(x, a, b); }\n"
                    else
                        out[#out + 1] = v .. " xt_" .. name .. "L(" .. v .. " x, " .. s .. " y) { return " .. name .. "(x, y); }\n"
                    end
                end
            end
        end
    end
    return table.concat(out)
end

-- Values per uniform type, for initializers
local typeComponents = {
    float = 1, int = 1, uint = 1, bool = 1,
    vec2 = 2, vec3 = 3, vec4 = 4, ivec2 = 2, ivec3 = 3, ivec4 = 4,
    uvec2 = 2, uvec3 = 3, uvec4 = 4, bvec2 = 2, bvec3 = 3, bvec4 = 4,
    mat2 = 4, mat3 = 9, mat4 = 16, mat2x2 = 4, mat3x3 = 9, mat4x4 = 16,
    mat2x3 = 6, mat2x4 = 8, mat3x2 = 6, mat3x4 = 12, mat4x2 = 8, mat4x3 = 12,
}

local function constant(text)
    text = string.match(text, "^%s*(.-)%s*$")
    if text == "true" then
        return 1
    elseif text == "false" then
        return 0
    end
    return tonumber((string.gsub(text, "[fFuU]$", "")))
end

-- An initializer's values ("1.0", "vec2(800.0, 600.0)", "vec4(1.0)", "mat3(1.0)"),
-- as GL sets them (matrices column by column); nil unless it is all numbers
local function initializerValues(typeName, expr)
    local comps = typeComponents[typeName]
    if not comps then
        return nil
    end

    local value = constant(expr)
    if value and comps == 1 then
        return { value }
    end

    local ctor, args = string.match(expr, "^%s*([%w_]+)%s*(%b())%s*$")
    if ctor ~= typeName then
        return nil
    end
    local values = {}
    for arg in string.gmatch(string.sub(args, 2, -2), "[^,]+") do
        values[#values + 1] = constant(arg)
        if values[#values] == nil then
            return nil
        end
    end

    if #values == comps then
        return values
    elseif #values == 1 then
        local columns, rows = string.match(typeName, "^mat(%d)x?(%d?)$")
        local out = {}
        if columns then
            -- matN(x): x down the diagonal
            columns, rows = tonumber(columns), tonumber(rows) or tonumber(columns)
            for c = 0, columns - 1 do
                for r = 0, rows - 1 do
                    out[#out + 1] = (c == r) and values[1] or 0
                end
            end
        else
            for i = 1, comps do
                out[i] = values[1]
            end
        end
        return out
    end
    return nil
end

-- Takes "= value" off uniform declarations (ES has no uniform initializers);
-- inits gets "name=v,v,..." for each, dropped the names whose value isn't numbers
local function takeInitializers(source, inits, dropped)
    return (string.gsub(source, "%f[%w_](uniform%s[^;{}=]-)%s*=%s*([^;{}]+);", function(decl, expr)
        local typeName, name = string.match(decl, "([%w_]+)%s+([%a_][%w_]*)%s*$")
        local values = name and initializerValues(typeName, expr)
        if values then
            local text = {}
            for i, v in ipairs(values) do
                text[i] = string.format("%.9g", v)
            end
            inits[name] = name .. "=" .. table.concat(text, ",")
        else
            dropped[#dropped + 1] = name or decl
        end
        return decl .. ";"
    end))
end

-- An array size as SMBX2's preprocessor leaves it ("128*16 >> 2"); nil unless
-- it is whole-number arithmetic
local function evalSize(expr)
    expr = string.match(expr, "^%s*(.-)%s*$")
    if not string.find(expr, "^[%d%s%+%-%*/%%%(%)<>]+$") then
        return nil
    end
    -- shifts bind looser than the rest: split at the last one outside brackets
    local depth, at, op, i = 0, nil, nil, 1
    while i <= #expr do
        local c, two = string.sub(expr, i, i), string.sub(expr, i, i + 1)
        if c == "(" then
            depth = depth + 1
        elseif c == ")" then
            depth = depth - 1
        elseif depth == 0 and (two == ">>" or two == "<<") then
            at, op = i, two
            i = i + 1
        end
        i = i + 1
    end
    if at then
        local a, b = evalSize(string.sub(expr, 1, at - 1)), evalSize(string.sub(expr, at + 2))
        if not a or not b then
            return nil
        end
        return op == ">>" and math.floor(a / 2 ^ b) or a * 2 ^ b
    end
    if string.find(expr, "[<>]") then
        return nil
    end
    local f = loadstring("return " .. expr)
    local ok, v = pcall(f)
    if ok and type(v) == "number" and v == math.floor(v) then
        return v
    end
    return nil
end

local packedTypes = { uint = "uvec4", int = "ivec4" }

-- Rewrites each use a[i] of the packed arrays (also inside other indices)
local function packedUses(source, names)
    for name in pairs(names) do
        source = string.gsub(source, word(name) .. "%s*(%b[])", function(index)
            local i = "int(" .. packedUses(string.sub(index, 2, -2), names) .. ")"
            return "xt_p4_" .. name .. "[" .. i .. " >> 2][" .. i .. " & 3]"
        end)
    end
    return source
end

-- Adreno (the AYN Odin's GPU) packs uniform arrays of single ints four to a
-- vector and then reads them wrong by a dynamic index: SMATRS's map terrain
-- (uint arrays) drew nothing there, while its vector arrays read fine. For ES,
-- "uniform uint[N] a;" (N a multiple of 4) becomes "uniform uvec4 xt_p4_a[N/4];"
-- and each a[i] xt_p4_a[int(i) >> 2][int(i) & 3]; lunadll.lua reports it as a
-- uvec4 array, which SMBX2's ffi_graphics fills from the same N numbers.
-- Arrays used other than by index stay as they are. Returns both sources and
-- the names packed ("a[N]").
local function packScalarArrays(vs, fs)
    local sizes, uses = {}, {}
    local function scan(source)
        local function found(typeName, name, spec)
            local n = evalSize(string.sub(spec, 2, -2))
            if n and n > 0 and n % 4 == 0 and sizes[name] ~= false then
                sizes[name] = { typeName, n }
            else
                sizes[name] = false
            end
        end
        for typeName, spec, name in string.gmatch(source, "%f[%w_]uniform%s+(u?int)%s*(%b[])%s*([%a_][%w_]*)%s*;") do
            found(typeName, name, spec)
        end
        for typeName, name, spec in string.gmatch(source, "%f[%w_]uniform%s+(u?int)%s+([%a_][%w_]*)%s*(%b[])%s*;") do
            found(typeName, name, spec)
        end
    end
    scan(vs)
    scan(fs)

    local names, list = {}, {}
    for name, info in pairs(sizes) do
        if info then
            -- every use but the declarations must be an index
            local ok = true
            for _, source in ipairs({ vs, fs }) do
                local rest = string.gsub(source, "%f[%w_]uniform%s+u?int%s*%b[]%s*" .. word(name) .. "%s*;", "")
                rest = string.gsub(rest, "%f[%w_]uniform%s+u?int%s+" .. word(name) .. "%s*%b[]%s*;", "")
                local _, all = string.gsub(rest, word(name), "")
                local _, indexed = string.gsub(rest, word(name) .. "%s*%[", "")
                if all ~= indexed then
                    ok = false
                end
            end
            if ok then
                names[name] = info
                list[#list + 1] = name .. "[" .. info[2] .. "]"
            end
        end
    end
    if #list == 0 then
        return vs, fs, list
    end

    local function declare(typeName, name)
        local info = names[name]
        return "uniform " .. packedTypes[info[1]] .. " xt_p4_" .. name .. "[" .. (info[2] / 4) .. "];"
    end
    local function pack(source)
        source = string.gsub(source, "%f[%w_]uniform%s+(u?int)%s*%b[]%s*([%a_][%w_]*)%s*;", function(typeName, name)
            return names[name] and declare(typeName, name)
        end)
        source = string.gsub(source, "%f[%w_]uniform%s+(u?int)%s+([%a_][%w_]*)%s*%b[]%s*;", function(typeName, name)
            return names[name] and declare(typeName, name)
        end)
        return packedUses(source, names)
    end
    table.sort(list)
    return pack(vs), pack(fs), list
end

-- vertexSource, fragmentSource: what SMBX2 hands FFI_ShaderFromStrings (already
-- run through its preprocessor). header: xtech_gfx_shaderHeader().
-- Returns the vertex and fragment sources to compile, the uniform initializers
-- taken out for ES ("name=v,v,...\n" lines, "" if none), a list of the
-- uniforms whose initializers were dropped and one of the arrays packed
-- (packScalarArrays).
function glsl.translate(vertexSource, fragmentSource, header)
    local targetVersion = tonumber(string.match(header, "#version%s+(%d+)")) or 100
    local modern = targetVersion >= 300
    local es = string.find(header, " es") ~= nil or targetVersion == 100

    local vs, vsCoords = translateCommon(vertexSource, modern, sourceVersion(vertexSource))
    local fs, fsCoords = translateCommon(fragmentSource, modern, sourceVersion(fragmentSource))
    local coords = math.max(vsCoords, fsCoords, 0)

    local inits, dropped, packed = {}, {}, {}
    local vsHelpers, fsHelpers = {}, {}
    if es then
        vs = takeInitializers(vs, inits, dropped)
        fs = takeInitializers(fs, inits, dropped)
        vs = floatArguments(vs, sourceVersion(vertexSource) >= 130, vsHelpers)
        fs = floatArguments(fs, sourceVersion(fragmentSource) >= 130, fsHelpers)
        if modern then
            vs, fs, packed = packScalarArrays(vs, fs)
        end
    end
    local initLines = {}
    for _, line in pairs(inits) do
        initLines[#initLines + 1] = line .. "\n"
    end
    table.sort(initLines)

    vs = string.gsub(vs, word("ftransform") .. "%s*%(%s*%)", "(xt_MVP * xt_Vertex)")
    vs = swap(vs, "gl_Vertex", "xt_Vertex")
    vs = swap(vs, "gl_MultiTexCoord0", "xt_MultiTexCoord0")
    vs = swap(vs, "gl_Color", "xt_Color")
    vs = swap(vs, "gl_FrontColor", "xt_FrontColor")
    local clipVertex = string.find(vs, word("gl_ClipVertex")) ~= nil
    vs = swap(vs, "gl_ClipVertex", "xt_ClipVertex")

    fs = swap(fs, "gl_Color", "xt_FrontColor")

    local attribute, varyingOut, varyingIn = "attribute", "varying", "varying"
    if modern then
        attribute, varyingOut, varyingIn = "in", "out", "in"
        vs = swap(vs, "attribute", "in")
        vs = swap(vs, "varying", "out")
        fs = swap(fs, "varying", "in")
        fs = swap(fs, "gl_FragColor", "xt_FragColor")
        fs = string.gsub(fs, "gl_FragData%s*%[%s*0%s*%]", "xt_FragColor")
    end

    local vsHead = { header }
    local fsHead = { header }

    if es then
        vsHead[#vsHead + 1] = "precision highp float;\nprecision highp int;\n"
        if modern then
            fsHead[#fsHead + 1] = "precision highp float;\nprecision highp int;\n"
        else
            fsHead[#fsHead + 1] = "#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n" ..
                                  "#else\nprecision mediump float;\n#endif\n"
        end
        vsHead[#vsHead + 1] = limitHelpers(vsHelpers)
        fsHead[#fsHead + 1] = limitHelpers(fsHelpers)
    end

    vsHead[#vsHead + 1] = attribute .. " vec4 xt_Vertex;\n" ..
                          attribute .. " vec4 xt_MultiTexCoord0;\n" ..
                          attribute .. " vec4 xt_Color;\n" ..
                          "uniform mat4 xt_MVP;\nuniform mat4 xt_MV;\nuniform mat4 xt_P;\n" ..
                          "uniform mat4 xt_TexMatrix0;\n" ..
                          varyingOut .. " vec4 xt_FrontColor;\n"
    fsHead[#fsHead + 1] = varyingIn .. " vec4 xt_FrontColor;\n"

    for n = 0, coords do
        vsHead[#vsHead + 1] = varyingOut .. " vec4 xt_TexCoord" .. n .. ";\n"
        fsHead[#fsHead + 1] = varyingIn .. " vec4 xt_TexCoord" .. n .. ";\n"
    end

    if clipVertex then
        vsHead[#vsHead + 1] = "vec4 xt_ClipVertex;\n"
    end

    -- clip planes: our main() around the shader's
    vs = string.gsub(vs, "void(%s+)main(%s*%()", "void%1xt_main%2")
    fs = string.gsub(fs, "void(%s+)main(%s*%()", "void%1xt_main%2")
    vsHead[#vsHead + 1] = varyingOut .. " vec4 xt_ClipPos;\n"
    fsHead[#fsHead + 1] = varyingIn .. " vec4 xt_ClipPos;\n" ..
                          "uniform vec4 xt_ClipPlane[6];\nuniform int xt_ClipPlanes;\n"
    vs = vs .. "\nvoid main()\n{\n    xt_main();\n    xt_ClipPos = " ..
         (clipVertex and "xt_ClipVertex" or "xt_MV * xt_Vertex") .. ";\n}\n"
    fs = fs .. "\nvoid main()\n{\n" ..
         "    if(xt_ClipPlanes > 0)\n    {\n" ..
         "        for(int i = 0; i < 6; i++)\n        {\n" ..
         "            if(i < xt_ClipPlanes && dot(xt_ClipPlane[i], xt_ClipPos) < 0.0)\n" ..
         "                discard;\n        }\n    }\n" ..
         "    xt_main();\n}\n"

    if string.find(fs, word("xt_MVP")) then
        fsHead[#fsHead + 1] = "uniform mat4 xt_MVP;\n"
    end
    if string.find(fs, word("xt_MV")) then
        fsHead[#fsHead + 1] = "uniform mat4 xt_MV;\n"
    end
    if string.find(fs, word("xt_P")) then
        fsHead[#fsHead + 1] = "uniform mat4 xt_P;\n"
    end
    if string.find(fs, word("xt_TexMatrix0")) then
        fsHead[#fsHead + 1] = "uniform mat4 xt_TexMatrix0;\n"
    end

    if modern then
        fsHead[#fsHead + 1] = "out vec4 xt_FragColor;\n"
    end

    return table.concat(vsHead) .. vs, table.concat(fsHead) .. fs, table.concat(initLines), dropped, packed
end

-- A reported variable name as SMBX2 knows it; nil for the translation's own
function glsl.scriptName(name)
    if string.sub(name, 1, 5) == "xt_r_" then
        return string.sub(name, 6)
    elseif string.sub(name, 1, 6) == "xt_p4_" then -- packScalarArrays
        return string.sub(name, 7)
    elseif string.find(name, "^xt_") or string.find(name, "^gl_") then
        return nil
    end
    return name
end

return glsl
