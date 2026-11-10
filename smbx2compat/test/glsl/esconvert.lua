-- Translates SMBX2's and SMATRS's shaders for OpenGL ES without implicit
-- int -> float conversions (as on the Retroid Pocket Nova's driver), with and
-- without glsltypes.lua, and compiles both with glslc (the Android NDK's, or
-- any glslang-based one), which reports the same type errors as Adreno.
--
--   luajit esconvert.lua [name filter]
--
-- GLSL_CORPUS: a folder of the shader files with their paths flattened
-- ("data__scripts__shaders__darkness__darkness.frag", "data__worlds__SMATRS
-- Chapter 1+2___shaders__lib3d__project.frag"); game data, so not in git
-- (default ~/ikoonat-shader-corpus). GLSLC: the compiler (default the NDK's).
-- V=1 lists every shader. It prints a summary line: how many failed with the
-- translation as before and as now, fixed, broken.
local here = arg[0]:match("^(.*)/") or "."
local CORE = here .. "/../../core/"
local HOME = os.getenv("HOME")
local CORPUS = os.getenv("GLSL_CORPUS") or (HOME .. "/ikoonat-shader-corpus")
local GLSLC = os.getenv("GLSLC")
if not GLSLC then
    local p = io.popen("ls -d " .. HOME .. "/Android/Sdk/ndk/*/shader-tools/*/glslc 2>/dev/null | tail -1")
    GLSLC = p:read("*l")
    p:close()
end
assert(GLSLC, "no glslc: set GLSLC")
local ROOT = "/tmp/esconvert-tree"
local WORK = "/tmp/esconvert-work"
local filter = arg[1]
local verbose = os.getenv("V")

local glsl = dofile(CORE .. "glsl.lua")
glsl.types = dofile(CORE .. "glsltypes.lua")
local pp = assert(loadfile(here .. "/pp.lua"))(glsl.types)

local function sh(cmd)
    local p = io.popen(cmd .. " 2>&1")
    local out = p:read("*a")
    p:close()
    return out
end
local function q(s)
    return "'" .. s:gsub("'", "'\\''") .. "'"
end
local function readFile(path)
    local f = io.open(path, "rb")
    if not f then return nil end
    local s = f:read("*a")
    f:close()
    return s
end
local function writeFile(path, s)
    local f = assert(io.open(path, "wb"))
    f:write(s)
    f:close()
end

-- the tree, from the flattened names
sh("rm -rf " .. ROOT .. " " .. WORK .. "; mkdir -p " .. ROOT .. " " .. WORK)
local files = {}
for name in io.popen("ls " .. q(CORPUS)):lines() do
    local path = name:gsub("__", "/")
    local dir = path:match("^(.*)/")
    sh("mkdir -p " .. q(ROOT .. "/" .. dir) .. " && cp " .. q(CORPUS .. "/" .. name) .. " " .. q(ROOT .. "/" .. path))
    files[#files + 1] = path
end
local EP = ROOT .. "/data/worlds/SMATRS Chapter 1+2"

-- includes from the file's folder, the level, the episode, SMBX2's scripts
local function preprocess(path, defines, levelDir)
    local text = readFile(path)
    local version = text:match("#[ \t]*version[ \t]+(%d+)") or "110"
    local inc = { EP, ROOT .. "/data/scripts", ROOT .. "/data" }
    if levelDir then table.insert(inc, 1, levelDir) end
    local out, err = pp(path, defines, inc)
    if not out then
        return nil, err
    end
    return "#version " .. version .. "\n" .. out
end

local function compile(source, stage)
    -- glslc takes ES from 3.10; the type rules are 3.00's
    source = source:gsub("#version 300 es", "#version 310 es", 1)
    local f = WORK .. "/c." .. stage
    writeFile(f, source)
    local out = sh(GLSLC .. " --target-env=opengl -fauto-map-locations -fauto-bind-uniforms -fshader-stage=" .. stage ..
                   " -o /dev/null " .. q(f))
    local errors = {}
    for line in out:gmatch("[^\n]+") do
        if line:find("error:") and not line:find("errors? generated") then
            errors[#errors + 1] = line:gsub("^[^:]*:", "")
        end
    end
    return errors
end

-- each .frag with the standard vertex shader, each .vert with the standard
-- fragment one, darkness.frag with each falloff and shadow kind, each lib3d
-- SHADER of SMATRS's map with the episode's project.vert/frag (sizes as
-- map.lua sets them)
local header = "#version 300 es\n"
local stdVert = ROOT .. "/data/_templates/standard.vert"
local stdFrag = ROOT .. "/data/_templates/standard.frag"
local jobs = {}
for _, path in ipairs(files) do
    local full = ROOT .. "/" .. path
    if path:match("darkness/darkness%.frag$") then
        for _, falloff in ipairs({ "hard", "lin", "sig", "sqr", "sqrstep", "step" }) do
            for _, shadow in ipairs({ "none", "raymarch", "raymarch_hard" }) do
                jobs[#jobs + 1] = { name = path .. " " .. falloff .. "/" .. shadow, vs = stdVert, fs = full,
                                    defines = { FALLOFF = '"shaders/darkness/falloff_' .. falloff .. '.glsl"',
                                                SHADOWS = '"shaders/darkness/shadow_' .. shadow .. '.glsl"',
                                                _MAXLIGHTS = "10", ADDITIVE_BRIGHTNESS = "0" } }
            end
        end
    elseif path:match("%.frag$") and not path:match("lib3d/project") then
        jobs[#jobs + 1] = { name = path, vs = stdVert, fs = full, defines = { CHECK_COUNT = "8" } }
    elseif path:match("%.vert$") and not path:match("lib3d/project") then
        jobs[#jobs + 1] = { name = path, vs = full, fs = stdFrag }
    elseif path:match("World Map/map/.*%.glsl$") and not path:match("Effects%.glsl$") then
        local rel = path:match("World Map/(map/.*)$")
        for _, alpha in ipairs({ "ALPHA_OPAQUE", "ALPHA_CUTOFF" }) do
            jobs[#jobs + 1] = { name = "lib3d:" .. rel .. " " .. alpha, vs = EP .. "/_shaders/lib3d/project.vert",
                                fs = EP .. "/_shaders/lib3d/project.frag",
                                defines = { SHADER = '"' .. rel .. '"', MAX_PIECES_IN_GROUP = "128",
                                            MAX_JOINED_PIECES = "96", TYPE_COUNT = "16", ALPHAMODE = alpha },
                                level = EP .. "/World Map" }
        end
    end
end

local stats = { jobs = 0, ppfail = 0, before = 0, after = 0, fixed = 0, broke = 0, notTyped = 0 }
for _, job in ipairs(jobs) do
    if not filter or job.name:find(filter, 1, true) then
        local vs, e1 = preprocess(job.vs, job.defines, job.level)
        local fs, e2 = preprocess(job.fs, job.defines, job.level)
        stats.jobs = stats.jobs + 1
        if not vs or not fs then
            stats.ppfail = stats.ppfail + 1
            print("PREPROCESSING FAILED " .. job.name .. ": " .. tostring(e1 or e2))
        else
            local bv, bf = glsl.translate(vs, fs, header, { conversions = false })
            local av, af, _, _, _, conv = glsl.translate(vs, fs, header)
            local before = #compile(bv, "vert") + #compile(bf, "frag")
            local ev, ef = compile(av, "vert"), compile(af, "frag")
            local after = #ev + #ef
            if before > 0 then stats.before = stats.before + 1 end
            if after > 0 then stats.after = stats.after + 1 end
            if before > 0 and after == 0 then stats.fixed = stats.fixed + 1 end
            if before == 0 and after > 0 then stats.broke = stats.broke + 1 end
            if #conv.failed > 0 then stats.notTyped = stats.notTyped + 1 end
            if after > 0 or #conv.failed > 0 or verbose then
                print(string.format("%-90s before %2d after %2d conversions %3d %s", job.name, before, after,
                                    conv.count, table.concat(conv.failed, "; ")))
                for i = 1, math.min(#ev, 3) do print("    vertex " .. ev[i]) end
                for i = 1, math.min(#ef, 4) do print("    fragment " .. ef[i]) end
            end
        end
    end
end
print(string.format("shaders %d (preprocessing failed %d); failing before %d, now %d; fixed %d, broken %d; types not followed in %d",
                    stats.jobs, stats.ppfail, stats.before, stats.after, stats.fixed, stats.broke, stats.notTyped))
