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

What isn't handled yet: ES's lack of implicit int -> float conversion (SMBX2's
shaders rely on it; desktop GLSL allows it), clip planes, and built-ins
besides the ones above. A shader that doesn't compile is drawn without.
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

-- vertexSource, fragmentSource: what SMBX2 hands FFI_ShaderFromStrings (already
-- run through its preprocessor). header: xtech_gfx_shaderHeader().
-- Returns the vertex and fragment sources to compile.
function glsl.translate(vertexSource, fragmentSource, header)
    local targetVersion = tonumber(string.match(header, "#version%s+(%d+)")) or 100
    local modern = targetVersion >= 300
    local es = string.find(header, " es") ~= nil or targetVersion == 100

    local vs, vsCoords = translateCommon(vertexSource, modern, sourceVersion(vertexSource))
    local fs, fsCoords = translateCommon(fragmentSource, modern, sourceVersion(fragmentSource))
    local coords = math.max(vsCoords, fsCoords, 0)

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

    return table.concat(vsHead) .. vs, table.concat(fsHead) .. fs
end

-- A reported variable name as SMBX2 knows it; nil for the translation's own
function glsl.scriptName(name)
    if string.sub(name, 1, 5) == "xt_r_" then
        return string.sub(name, 6)
    elseif string.find(name, "^xt_") or string.find(name, "^gl_") then
        return nil
    end
    return name
end

return glsl
