--[[
glsltypes.lua: GLSL's implicit int -> float conversions, made explicit.

Desktop GLSL (1.20 on) converts ints to floats where a float is needed
("float a = 0;", "x * 2", "i < count" with a float count, "max(x, 0)");
OpenGL ES doesn't. GPUs with GL_EXT_shader_implicit_conversions take such
code anyway (the AYN Odin's Adreno 830), others don't compile it (the
Retroid Pocket Nova's Adreno 740 driver: SMATRS's map, its intro book and
much of its lighting drew nothing). convert() reads the shader's types and
writes each conversion out: "0" -> "0.0", an int expression e -> float(e),
an ivecN one -> vecN(e).

It follows the declarations (globals, locals, function parameters, structs)
and the preprocessor's active branches (#define, #if, #ifdef, ...), works out
each expression's type, and converts where desktop GLSL would: arithmetic
and comparisons mixing the two, assignments and initializers, returns,
?: branches, arguments to the shader's own functions, and arguments to the
built-in functions. GLSL before 1.30 has float versions of the built-ins
only, so there every int argument is converted (max(i, 0) / 2 is a float
division there); from 1.30 abs/sign/min/max/clamp also take ints, and a call
of ints only stays one.

Anything it can't follow (an unknown identifier's type, a function-like
macro) is left as it is. Source it can't parse is returned unchanged, with
the reason.
]]

local types = {}

------------------------------------------------------------------------------
-- Tokens

local twoCharOps = {}
for op in string.gmatch("++ -- << >> <= >= == != && || ^^ += -= *= /= %= &= |= ^=", "%S+") do
    twoCharOps[op] = true
end

-- Every token keeps its text, so the source comes back as it was, plus the
-- conversions. kind: "space" (whitespace and comments), "pp" (a preprocessor
-- line), "id", "num", "op"
local function tokenize(source)
    local list = {}
    local i, n = 1, #source
    local lineStart = true
    while i <= n do
        local c = string.sub(source, i, i)
        local s, e
        if c == "#" and lineStart then
            -- to the end of the line, continued by backslashes
            e = i
            while true do
                local nl = string.find(source, "\n", e, true)
                if not nl then
                    e = n
                    break
                end
                local before = string.sub(source, nl - 1, nl - 1)
                if before == "\\" or (before == "\r" and string.sub(source, nl - 2, nl - 2) == "\\") then
                    e = nl + 1
                else
                    e = nl - 1
                    break
                end
            end
            list[#list + 1] = { kind = "pp", text = string.sub(source, i, e) }
            i = e + 1
            lineStart = false
        elseif string.find(c, "%s") then
            s, e = string.find(source, "^%s+", i)
            local text = string.sub(source, s, e)
            list[#list + 1] = { kind = "space", text = text }
            if string.find(text, "\n") then
                lineStart = true
            end
            i = e + 1
        elseif string.sub(source, i, i + 1) == "//" then
            e = string.find(source, "\n", i, true)
            e = e and e - 1 or n
            list[#list + 1] = { kind = "space", text = string.sub(source, i, e) }
            i = e + 1
        elseif string.sub(source, i, i + 1) == "/*" then
            local _, close = string.find(source, "*/", i + 2, true)
            e = close or n
            list[#list + 1] = { kind = "space", text = string.sub(source, i, e) }
            i = e + 1
        else
            lineStart = false
            local text
            s, e = string.find(source, "^[%a_][%w_]*", i)
            if s then
                text = string.sub(source, s, e)
                list[#list + 1] = { kind = "id", text = text }
            else
                -- numbers: 0x1F, 12, 12u, 1.5, .5, 1e3, 1.5e-3f
                s, e = string.find(source, "^0[xX]%x+[uU]?", i)
                if not s then
                    s, e = string.find(source, "^%d+%.?%d*", i)
                    if not s then
                        s, e = string.find(source, "^%.%d+", i)
                    end
                    if s then
                        local _, ex = string.find(source, "^[eE][%+%-]?%d+", e + 1)
                        e = ex or e
                        local _, sx = string.find(source, "^[fFuU]", e + 1)
                        e = sx or e
                    end
                end
                if s then
                    list[#list + 1] = { kind = "num", text = string.sub(source, s, e) }
                else
                    local three = string.sub(source, i, i + 2)
                    local two = string.sub(source, i, i + 1)
                    if three == "<<=" or three == ">>=" then
                        text = three
                    elseif twoCharOps[two] then
                        text = two
                    else
                        text = c
                    end
                    e = i + #text - 1
                    list[#list + 1] = { kind = "op", text = text }
                end
            end
            i = e + 1
        end
    end
    return list
end

-- A number token's type
local function numberType(text)
    if string.find(text, "^0[xX]") then
        return string.find(text, "[uU]$") and "uint" or "int"
    elseif string.find(text, "[uU]$") then
        return "uint"
    elseif string.find(text, "[%.eEfF]") then
        return "float"
    end
    return "int"
end

------------------------------------------------------------------------------
-- The preprocessor: which tokens are compiled, and object-like macros

local function ppTokens(text)
    local out = {}
    for _, t in ipairs(tokenize(text)) do
        if t.kind ~= "space" then
            out[#out + 1] = t
        end
    end
    return out
end

-- Integer expression of an #if (defined X, macros, numbers); nil if it can't
local function evalIf(text, defines, depth)
    local toks = {}
    local list = ppTokens(text)
    local i = 1
    while i <= #list do
        local t = list[i]
        if t.kind == "id" and t.text == "defined" then
            local name
            if list[i + 1] and list[i + 1].text == "(" then
                name = list[i + 2] and list[i + 2].text
                i = i + 4
            else
                name = list[i + 1] and list[i + 1].text
                i = i + 2
            end
            toks[#toks + 1] = { kind = "num", text = defines[name] and "1" or "0" }
        elseif t.kind == "id" then
            local d = defines[t.text]
            if d and not d.params and (depth or 0) < 16 then
                local v = evalIf(d.body, defines, (depth or 0) + 1)
                if v == nil then
                    return nil
                end
                toks[#toks + 1] = { kind = "num", text = tostring(v) }
            elseif d then
                return nil
            else
                toks[#toks + 1] = { kind = "num", text = "0" }
            end
            i = i + 1
        else
            toks[#toks + 1] = t
            i = i + 1
        end
    end

    local pos = 1
    local function peek() return toks[pos] and toks[pos].text end
    local binary = {
        { "||" }, { "&&" }, { "|" }, { "^" }, { "&" }, { "==", "!=" },
        { "<", ">", "<=", ">=" }, { "<<", ">>" }, { "+", "-" }, { "*", "/", "%" },
    }
    local unaryExpr
    local function level(l)
        if l > #binary then
            return unaryExpr()
        end
        local a = level(l + 1)
        while true do
            local op = peek()
            local found = false
            for _, o in ipairs(binary[l]) do
                if o == op then
                    found = true
                end
            end
            if not found or a == nil then
                return a
            end
            pos = pos + 1
            local b = level(l + 1)
            if b == nil then
                return nil
            end
            if op == "||" then a = (a ~= 0 or b ~= 0) and 1 or 0
            elseif op == "&&" then a = (a ~= 0 and b ~= 0) and 1 or 0
            elseif op == "|" then a = bit.bor(a, b)
            elseif op == "^" then a = bit.bxor(a, b)
            elseif op == "&" then a = bit.band(a, b)
            elseif op == "==" then a = (a == b) and 1 or 0
            elseif op == "!=" then a = (a ~= b) and 1 or 0
            elseif op == "<" then a = (a < b) and 1 or 0
            elseif op == ">" then a = (a > b) and 1 or 0
            elseif op == "<=" then a = (a <= b) and 1 or 0
            elseif op == ">=" then a = (a >= b) and 1 or 0
            elseif op == "<<" then a = bit.lshift(a, b)
            elseif op == ">>" then a = bit.arshift(a, b)
            elseif op == "+" then a = a + b
            elseif op == "-" then a = a - b
            elseif op == "*" then a = a * b
            elseif op == "/" then if b == 0 then return nil end a = (a - a % b) / b
            elseif op == "%" then if b == 0 then return nil end a = math.fmod(a, b)
            end
        end
    end
    unaryExpr = function()
        local op = peek()
        if op == "!" or op == "-" or op == "+" or op == "~" then
            pos = pos + 1
            local v = unaryExpr()
            if v == nil then return nil end
            if op == "!" then return v == 0 and 1 or 0 end
            if op == "-" then return -v end
            if op == "~" then return bit.bnot(v) end
            return v
        elseif op == "(" then
            pos = pos + 1
            local v = level(1)
            if peek() ~= ")" then return nil end
            pos = pos + 1
            return v
        elseif toks[pos] and toks[pos].kind == "num" then
            pos = pos + 1
            return tonumber((string.gsub(toks[pos - 1].text, "[uU]$", "")))
        end
        return nil
    end
    local v = level(1)
    if v == nil or pos <= #toks then
        return nil
    end
    return v
end

-- Marks each token active or not by the #if branches; collects macros.
-- defines: name -> { params = list or nil, body = text }. Returns false and a
-- reason when it can't tell.
local function preprocess(tokens, defines)
    local stack = {} -- { active, taken, parent }
    local active = true
    for _, t in ipairs(tokens) do
        t.active = active
        if t.kind == "pp" then
            local text = string.gsub(t.text, "\\\r?\n", " ")
            local directive, rest = string.match(text, "^#%s*([%a_]*)%s*(.-)%s*$")
            if directive == "define" and active then
                local name, params, body = string.match(rest, "^([%a_][%w_]*)%(([^)]*)%)%s*(.*)$")
                if name then
                    local list = {}
                    for p in string.gmatch(params, "[%a_][%w_]*") do
                        list[#list + 1] = p
                    end
                    defines[name] = { params = list, body = body }
                else
                    name, body = string.match(rest, "^([%a_][%w_]*)%s*(.*)$")
                    if name then
                        defines[name] = { body = body }
                    end
                end
            elseif directive == "undef" and active then
                defines[string.match(rest, "^[%a_][%w_]*") or ""] = nil
            elseif directive == "ifdef" or directive == "ifndef" then
                local name = string.match(rest, "^[%a_][%w_]*")
                local cond = (defines[name] ~= nil) == (directive == "ifdef")
                stack[#stack + 1] = { parent = active, taken = cond }
                active = active and cond
            elseif directive == "if" then
                local v = 0
                if active then
                    v = evalIf(rest, defines)
                    if v == nil then
                        return false, "can't evaluate #if " .. rest
                    end
                end
                stack[#stack + 1] = { parent = active, taken = v ~= 0 }
                active = active and v ~= 0
            elseif directive == "elif" then
                local top = stack[#stack]
                if not top then
                    return false, "#elif without #if"
                end
                if top.taken or not top.parent then
                    active = false
                else
                    local v = evalIf(rest, defines)
                    if v == nil then
                        return false, "can't evaluate #elif " .. rest
                    end
                    top.taken = v ~= 0
                    active = v ~= 0
                end
            elseif directive == "else" then
                local top = stack[#stack]
                if not top then
                    return false, "#else without #if"
                end
                active = top.parent and not top.taken
                top.taken = true
            elseif directive == "endif" then
                local top = table.remove(stack)
                if not top then
                    return false, "#endif without #if"
                end
                active = top.parent
            end
        end
    end
    return true
end

------------------------------------------------------------------------------
-- Types: names ("float", "ivec3", "mat4", "sampler2D", a struct's name),
-- with array = true for arrays. nil: not known.

local shapes = {} -- name -> { base, n } for scalars and vectors, { "float", cols, rows } for matrices
for _, b in ipairs({ { "", "float" }, { "i", "int" }, { "u", "uint" }, { "b", "bool" } }) do
    shapes[b[2]] = { base = b[2], n = 1 }
    for n = 2, 4 do
        shapes[b[1] .. "vec" .. n] = { base = b[2], n = n }
    end
end
for c = 2, 4 do
    shapes["mat" .. c] = { base = "float", cols = c, rows = c, matrix = true }
    for r = 2, 4 do
        shapes["mat" .. c .. "x" .. r] = { base = "float", cols = c, rows = r, matrix = true }
    end
end

local function vectorName(base, n)
    if n == 1 then
        return base
    end
    local prefix = ({ float = "", int = "i", uint = "u", bool = "b" })[base]
    return prefix .. "vec" .. n
end

local function matrixName(cols, rows)
    return cols == rows and ("mat" .. cols) or ("mat" .. cols .. "x" .. rows)
end

local function T(name, array)
    return name and { name = name, array = array or nil } or nil
end

local function shapeOf(t)
    return t and not t.array and shapes[t.name]
end

local function isIntLike(t)
    local s = shapeOf(t)
    return s and not s.matrix and (s.base == "int" or s.base == "uint")
end

local function isFloatLike(t)
    local s = shapeOf(t)
    return s and s.base == "float"
end

-- The float type an int one converts to
local function floatOf(t)
    local s = shapeOf(t)
    return T(vectorName("float", s.n))
end

local builtinTypeNames = {}
for name in pairs(shapes) do
    builtinTypeNames[name] = true
end
for name in string.gmatch("void sampler1D sampler2D sampler3D samplerCube sampler1DShadow sampler2DShadow " ..
                          "sampler2DRect samplerCubeShadow sampler2DArray sampler2DArrayShadow " ..
                          "isampler2D isampler3D isamplerCube isampler2DArray usampler2D usampler3D " ..
                          "usamplerCube usampler2DArray samplerExternalOES", "%S+") do
    builtinTypeNames[name] = true
end

local qualifierWords = {}
-- (not later GLSL's "sample", "buffer", "shared", ...: GLSL 1.20 code may use
-- those as names)
for name in string.gmatch("const uniform attribute varying in out inout centroid flat smooth noperspective " ..
                          "invariant highp mediump lowp", "%S+") do
    qualifierWords[name] = true
end

-- Built-in variables (fixed-function ones too: glsl.lua swaps them after this)
local builtinVariables = {
    gl_Position = "vec4", gl_PointSize = "float", gl_FragCoord = "vec4", gl_FrontFacing = "bool",
    gl_FragColor = "vec4", gl_FragDepth = "float", gl_PointCoord = "vec2",
    gl_VertexID = "int", gl_InstanceID = "int",
    gl_Vertex = "vec4", gl_Color = "vec4", gl_SecondaryColor = "vec4", gl_Normal = "vec3",
    gl_MultiTexCoord0 = "vec4", gl_MultiTexCoord1 = "vec4", gl_FrontColor = "vec4",
    gl_BackColor = "vec4", gl_ClipVertex = "vec4", gl_FogFragCoord = "float",
    gl_ModelViewMatrix = "mat4", gl_ProjectionMatrix = "mat4", gl_ModelViewProjectionMatrix = "mat4",
    gl_NormalMatrix = "mat3",
    xt_MVP = "mat4", xt_MV = "mat4", xt_P = "mat4", xt_TexMatrix0 = "mat4",
    xt_Vertex = "vec4", xt_MultiTexCoord0 = "vec4", xt_Color = "vec4", xt_FrontColor = "vec4",
    xt_FragColor = "vec4",
}
local builtinArrays = { gl_FragData = "vec4", gl_TexCoord = "vec4", gl_TextureMatrix = "mat4" }

-- Built-in functions. kind:
--   "f"     float only: every int argument becomes a float
--   "fi"    float, int (and uint) versions (1.30 on): ints converted only when
--           floats are mixed in; before 1.30 as "f"
--   result: "first" (the first argument's type, after conversion), "last",
--           "float", "vec3", "vec4", "bool", "bvec" (bool vector of the
--           first's size), "same", "tex" (from the sampler), "ivec" (an int
--           vector of the first's size), "transpose"
local builtins = {}
local function def(names, kind, result, skip)
    for name in string.gmatch(names, "%S+") do
        builtins[name] = { kind = kind, result = result, skip = skip }
    end
end
def("radians degrees sin cos tan asin acos atan pow exp log exp2 log2 sqrt inversesqrt " ..
    "floor ceil fract trunc round roundEven sinh cosh tanh asinh acosh atanh dFdx dFdy fwidth " ..
    "normalize faceforward reflect refract mod matrixCompMult inverse", "f", "first")
def("mix", "f", "first")
def("step", "f", "last")
def("smoothstep", "f", "last")
def("length distance dot determinant", "f", "float")
def("cross", "f", "vec3")
def("abs sign min max clamp", "fi", "first")
def("lessThan lessThanEqual greaterThan greaterThanEqual equal notEqual", "fi", "bvec")
def("isnan isinf", "f", "bvec")
def("any all", "none", "bool")
def("not", "none", "first")
def("transpose", "f", "transpose")
def("floatBitsToInt", "f", "ivec")
def("texture2D texture texture2DProj textureProj texture2DLod textureLod textureGrad " ..
    "textureProjLod texture2DRect textureCube textureCubeLod shadow2D " ..
    "texture3D texture1D", "f", "tex", 1)
-- integer coordinates or offsets
def("texelFetch texelFetchOffset textureOffset textureLodOffset", "none", "tex")
def("textureSize", "none", "textureSize")

------------------------------------------------------------------------------
-- The parser

local Parser = {}
Parser.__index = Parser

local function fail(message)
    error({ glsltypes = message }, 0)
end

function Parser:peek(k)
    local i = self.code[self.pos + (k or 0)]
    return i and self.tokens[i].text, i and self.tokens[i].kind
end

function Parser:next()
    local i = self.code[self.pos]
    if not i then
        fail("unexpected end")
    end
    self.pos = self.pos + 1
    return self.tokens[i].text, i
end

function Parser:expect(text)
    local got, i = self:next()
    if got ~= text then
        fail("expected '" .. text .. "' but found '" .. got .. "'")
    end
    return i
end

function Parser:accept(text)
    if self:peek() == text then
        self.pos = self.pos + 1
        return true
    end
    return false
end

function Parser:tokenIndex(offset)
    return self.code[self.pos + (offset or 0)]
end

-- Scopes
function Parser:push()
    self.scopes[#self.scopes + 1] = {}
end

function Parser:pop()
    self.scopes[#self.scopes] = nil
end

function Parser:declare(name, t)
    self.scopes[#self.scopes][name] = t or false
end

function Parser:lookup(name)
    for i = #self.scopes, 1, -1 do
        local t = self.scopes[i][name]
        if t ~= nil then
            return t or nil
        end
    end
    if builtinVariables[name] then
        return T(builtinVariables[name])
    elseif string.find(name, "^xt_TexCoord%d+$") or name == "xt_ClipVertex" then
        return T("vec4")
    elseif builtinArrays[name] then
        return T(builtinArrays[name], true)
    end
    local macro = self.defines[name]
    if macro and not macro.params then
        return self:macroType(name)
    end
    return nil
end

function Parser:isTypeName(name)
    return builtinTypeNames[name] or self.structs[name] ~= nil
end

-- An object-like macro's type, from its body as an expression
function Parser:macroType(name)
    local cached = self.macroTypes[name]
    if cached ~= nil then
        return cached or nil
    end
    self.macroTypes[name] = false -- against recursion
    local body = self.defines[name].body
    local t
    if body and body ~= "" then
        local sub = setmetatable({
            tokens = tokenize(body), code = {}, pos = 1, scopes = self.scopes,
            structs = self.structs, functions = self.functions, defines = self.defines,
            macroTypes = self.macroTypes, version = self.version,
            prefix = {}, suffix = {}, replace = {}, count = 0,
        }, Parser)
        for i, tok in ipairs(sub.tokens) do
            if tok.kind ~= "space" and tok.kind ~= "pp" then
                sub.code[#sub.code + 1] = i
            end
        end
        local ok, e = pcall(function()
            local node = sub:expression()
            if sub.pos <= #sub.code then
                fail("more")
            end
            return node
        end)
        if ok and e then
            t = e.type
        end
    end
    self.macroTypes[name] = t or false
    return t
end

------------------------------------------------------------------------------
-- Conversions

-- An expression node: { type, first, last (token indices), literal (a decimal
-- int literal's token), wrapped (a unary +/- or brackets around one) }
function Parser:convert(node, to)
    if not node or not isIntLike(node.type) then
        return node
    end
    to = to or floatOf(node.type)
    self.count = self.count + 1
    local lit = node.literal
    if lit then
        local text = self.tokens[lit].text
        if string.find(text, "^%d+$") then
            self.replace[lit] = text .. ".0"
            node.type = to
            return node
        end
    end
    self.prefix[node.first] = to.name .. "(" .. (self.prefix[node.first] or "")
    self.suffix[node.last] = (self.suffix[node.last] or "") .. ")"
    node.type = to
    node.literal = nil
    return node
end

-- For two operands where one is a float type and the other an int type
function Parser:balance(a, b)
    if isFloatLike(a.type) and isIntLike(b.type) then
        self:convert(b)
    elseif isIntLike(a.type) and isFloatLike(b.type) then
        self:convert(a)
    end
end

-- Converts node to type t if desktop GLSL would (int -> float, same size)
function Parser:convertTo(node, t)
    local from, to = shapeOf(node and node.type), shapeOf(t)
    if from and to and not to.matrix and to.base == "float" and isIntLike(node.type) and from.n == to.n then
        self:convert(node, T(t.name))
    end
end

------------------------------------------------------------------------------
-- Expressions

local function node(t, first, last)
    return { type = t, first = first, last = last }
end

function Parser:expression()
    local n = self:assignment()
    while self:peek() == "," do
        self:next()
        local b = self:assignment()
        n = node(b.type, n.first, b.last)
    end
    return n
end

local assignOps = { ["="] = true, ["+="] = true, ["-="] = true, ["*="] = true, ["/="] = true,
                    ["%="] = true, ["<<="] = true, [">>="] = true, ["&="] = true, ["^="] = true, ["|="] = true }

function Parser:assignment()
    local lhs = self:conditional()
    local op = self:peek()
    if assignOps[op] then
        self:next()
        local rhs = self:assignment()
        if op == "=" then
            self:convertTo(rhs, lhs.type)
        elseif op == "+=" or op == "-=" or op == "*=" or op == "/=" then
            if isFloatLike(lhs.type) and isIntLike(rhs.type) then
                self:convert(rhs)
            end
        end
        return node(lhs.type, lhs.first, rhs.last)
    end
    return lhs
end

function Parser:conditional()
    local c = self:binary(1)
    if self:peek() == "?" then
        self:next()
        local a = self:assignment()
        self:expect(":")
        local b = self:assignment()
        self:balance(a, b)
        return node(a.type or b.type, c.first, b.last)
    end
    return c
end

local levels = {
    { "||" }, { "^^" }, { "&&" }, { "|" }, { "^" }, { "&" }, { "==", "!=" },
    { "<", ">", "<=", ">=" }, { "<<", ">>" }, { "+", "-" }, { "*", "/", "%" },
}
local levelOf = {}
for l, ops in ipairs(levels) do
    for _, op in ipairs(ops) do
        levelOf[op] = l
    end
end

-- The type of a * b etc. for float or int operands of the same base
local function arithmeticType(a, b, op)
    local sa, sb = shapeOf(a), shapeOf(b)
    if not sa or not sb then
        return nil
    end
    if op == "*" and sa.matrix and sb.matrix then
        return T(matrixName(sb.cols, sa.rows))
    elseif op == "*" and sa.matrix and not sb.matrix and sb.n > 1 then
        return T(vectorName("float", sa.rows))
    elseif op == "*" and sb.matrix and not sa.matrix and sa.n > 1 then
        return T(vectorName("float", sb.cols))
    elseif sa.matrix or (not sb.matrix and sa.n > 1) then
        return T(a.name)
    elseif sb.matrix or sb.n > 1 then
        return T(b.name)
    end
    return T(a.name)
end

function Parser:binary(l)
    if l > #levels then
        return self:unary()
    end
    local a = self:binary(l + 1)
    while true do
        local op, kind = self:peek()
        if kind ~= "op" or levelOf[op] ~= l then
            return a
        end
        self:next()
        local b = self:binary(l + 1)
        local t
        if l <= 3 then
            t = T("bool")
        elseif op == "==" or op == "!=" or op == "<" or op == ">" or op == "<=" or op == ">=" then
            self:balance(a, b)
            t = T("bool")
        elseif op == "+" or op == "-" or op == "*" or op == "/" then
            self:balance(a, b)
            t = arithmeticType(a.type, b.type, op)
        else
            -- % & | ^ << >>: ints only
            t = arithmeticType(a.type, b.type, op)
            if op == "<<" or op == ">>" then
                t = a.type
            end
        end
        a = node(t, a.first, b.last)
    end
end

function Parser:unary()
    local op, kind = self:peek()
    if kind == "op" and (op == "-" or op == "+" or op == "!" or op == "~" or op == "++" or op == "--") then
        local first = self:tokenIndex()
        self:next()
        local inner = self:unary()
        local n = node(op == "!" and T("bool") or inner.type, first, inner.last)
        if (op == "-" or op == "+") and inner.literal then
            n.literal = inner.literal
        end
        return n
    end
    return self:postfix()
end

-- Arguments of a call: a list of nodes; at the closing bracket
function Parser:arguments()
    local args = {}
    if self:peek() == ")" then
        return args, self:next() and self:tokenIndex(-1)
    end
    if self:peek() == "void" and self:peek(1) == ")" then
        self:next()
        return args, select(2, self:next())
    end
    repeat
        args[#args + 1] = self:assignment()
    until not self:accept(",")
    local _, close = self:next()
    if self.tokens[close].text ~= ")" then
        fail("expected ')' after arguments")
    end
    return args, close
end

local function sameType(a, b)
    return a and b and a.name == b.name and (a.array or false) == (b.array or false)
end

local function convertible(from, to)
    local sf, st = shapeOf(from), shapeOf(to)
    return sf and st and isIntLike(from) and st.base == "float" and not st.matrix and sf.n == st.n
end

function Parser:callBuiltin(name, info, args)
    local skip = info.skip or 0
    local floatOnly = info.kind == "f" or (info.kind == "fi" and self.version < 130)
    if floatOnly then
        for i = skip + 1, #args do
            self:convert(args[i])
        end
    elseif info.kind == "fi" then
        local anyFloat = false
        for _, a in ipairs(args) do
            if isFloatLike(a.type) then
                anyFloat = true
            end
        end
        if anyFloat then
            for _, a in ipairs(args) do
                self:convert(a)
            end
        end
    end

    local r = info.result
    local first = args[1] and args[1].type
    if r == "first" then
        -- mod(vec3, float), min(vec2, 0.0): the vector
        for _, a in ipairs(args) do
            local s = shapeOf(a.type)
            if s and (s.n or 0) > 1 and name ~= "mix" then
                return T(a.type.name)
            end
        end
        return first and T(first.name)
    elseif r == "last" then
        local last = args[#args] and args[#args].type
        return last and T(last.name)
    elseif r == "float" or r == "vec3" or r == "vec4" or r == "bool" then
        return T(r)
    elseif r == "bvec" or r == "ivec" then
        local s = shapeOf(first)
        return s and T(vectorName(r == "bvec" and "bool" or "int", s.n))
    elseif r == "transpose" then
        local s = shapeOf(first)
        return s and s.matrix and T(matrixName(s.rows, s.cols))
    elseif r == "textureSize" then
        local sampler = first and first.name or ""
        if string.find(sampler, "Cube") or string.find(sampler, "2D") then
            return T(string.find(sampler, "Array") and "ivec3" or "ivec2")
        end
        return nil
    elseif r == "tex" then
        local sampler = first and first.name or ""
        if string.find(sampler, "Shadow") and name ~= "shadow2D" then
            return T("float")
        elseif string.find(sampler, "^isampler") then
            return T("ivec4")
        elseif string.find(sampler, "^usampler") then
            return T("uvec4")
        end
        return T("vec4")
    end
    return nil
end

function Parser:callFunction(name, args)
    local overloads = self.functions[name]
    if not overloads then
        return nil
    end
    local candidates = {}
    for _, f in ipairs(overloads) do
        if #f.params == #args then
            candidates[#candidates + 1] = f
        end
    end
    -- an exact match first, then one the arguments convert to
    for _, f in ipairs(candidates) do
        local exact = true
        for i, a in ipairs(args) do
            if not sameType(a.type, f.params[i]) then
                exact = false
            end
        end
        if exact then
            return f.ret
        end
    end
    local found
    for _, f in ipairs(candidates) do
        local fits = true
        for i, a in ipairs(args) do
            if not (sameType(a.type, f.params[i]) or convertible(a.type, f.params[i]) or a.type == nil) then
                fits = false
            end
        end
        if fits then
            if found then
                return nil -- ambiguous to us: leave it
            end
            found = f
        end
    end
    if not found and #candidates == 1 then
        found = candidates[1]
    end
    if found then
        for i, a in ipairs(args) do
            if convertible(a.type, found.params[i]) then
                self:convert(a, T(found.params[i].name))
            end
        end
        return found.ret
    end
    return nil
end

-- "[" ... "]" after a type: an array size (not needed here)
function Parser:skipBrackets()
    local depth = 0
    repeat
        local t = self:next()
        if t == "[" then
            depth = depth + 1
        elseif t == "]" then
            depth = depth - 1
        end
    until depth == 0
end

function Parser:primary()
    local text, kind = self:peek()
    local first = self:tokenIndex()
    if kind == "num" then
        self:next()
        local n = node(T(numberType(text)), first, first)
        if numberType(text) == "int" then
            n.literal = first
        end
        return n
    elseif text == "true" or text == "false" then
        self:next()
        return node(T("bool"), first, first)
    elseif text == "(" then
        self:next()
        local inner = self:expression()
        local close = self:expect(")")
        local n = node(inner.type, first, close)
        n.literal = inner.literal
        return n
    elseif kind == "id" then
        self:next()
        -- constructors (vec3(...), float[3](...), Struct(...)) and calls
        local isType = self:isTypeName(text)
        local arrayCtor = false
        if isType and self:peek() == "[" then
            self:skipBrackets()
            arrayCtor = true
        end
        if self:peek() == "(" then
            self:next()
            local args, close = self:arguments()
            local t
            if isType then
                t = T(text, arrayCtor)
                if arrayCtor then
                    for _, a in ipairs(args) do
                        self:convertTo(a, T(text))
                    end
                end
            elseif self.functions[text] then
                t = self:callFunction(text, args)
            elseif builtins[text] and not self:lookup(text) then
                t = self:callBuiltin(text, builtins[text], args)
            end
            return node(t, first, close)
        end
        if isType then
            fail("type name '" .. text .. "' in an expression")
        end
        return node(self:lookup(text), first, first)
    end
    fail("unexpected '" .. tostring(text) .. "'")
end

function Parser:postfix()
    local n = self:primary()
    while true do
        local op = self:peek()
        if op == "[" then
            self:next()
            self:expression()
            local close = self:expect("]")
            local t = n.type
            local s = shapeOf(t)
            if t and t.array then
                t = T(t.name)
            elseif s and s.matrix then
                t = T(vectorName("float", s.rows))
            elseif s and s.n > 1 then
                t = T(s.base)
            else
                t = nil
            end
            n = node(t, n.first, close)
        elseif op == "." then
            self:next()
            local field, at = self:next()
            local t = n.type
            local s = shapeOf(t)
            if t and t.array and field == "length" then
                t = T("int")
            elseif t and not t.array and self.structs[t.name] then
                t = self.structs[t.name][field]
            elseif s and not s.matrix and string.find(field, "^[xyzw]+$") or
                   s and not s.matrix and string.find(field, "^[rgba]+$") or
                   s and not s.matrix and string.find(field, "^[stpq]+$") then
                t = #field <= 4 and T(vectorName(s.base, #field)) or nil
            else
                t = nil
            end
            if self:peek() == "(" then
                -- .length()
                self:next()
                local _, close = self:arguments()
                n = node(t, n.first, close)
            else
                n = node(t, n.first, at)
            end
        elseif op == "++" or op == "--" then
            local _, at = self:next()
            n = node(n.type, n.first, at)
        else
            return n
        end
    end
end

------------------------------------------------------------------------------
-- Declarations and statements

-- Qualifiers, layout(...), precision words; returns whether any were found
function Parser:qualifiers()
    local found = false
    while true do
        local text = self:peek()
        if text == "layout" then
            self:next()
            self:expect("(")
            local depth = 1
            while depth > 0 do
                local t = self:next()
                if t == "(" then
                    depth = depth + 1
                elseif t == ")" then
                    depth = depth - 1
                end
            end
            found = true
        elseif qualifierWords[text] then
            self:next()
            found = true
        else
            return found
        end
    end
end

-- A type: a name, or "struct Name { ... }"; returns the type and whether
-- it was a struct definition
function Parser:typeSpecifier()
    local text, kind = self:peek()
    if text == "struct" then
        self:next()
        local name
        if self:peek() ~= "{" then
            name = self:next()
        else
            self.anonymous = (self.anonymous or 0) + 1
            name = "<struct" .. self.anonymous .. ">"
        end
        self:expect("{")
        local members = {}
        while not self:accept("}") do
            self:qualifiers()
            local t = self:typeSpecifier()
            local arrayType = false
            if self:peek() == "[" then
                self:skipBrackets()
                arrayType = true
            end
            repeat
                local field = self:next()
                local isArray = arrayType
                if self:peek() == "[" then
                    self:skipBrackets()
                    isArray = true
                end
                members[field] = t and T(t.name, isArray)
            until not self:accept(",")
            self:expect(";")
        end
        self.structs[name] = members
        return T(name), true
    end
    if kind == "id" and self:isTypeName(text) then
        self:next()
        return T(text), false
    end
    fail("expected a type, found '" .. tostring(text) .. "'")
end

-- Whether a declaration starts here (in a statement)
function Parser:declarationAhead()
    local text, kind = self:peek()
    if kind ~= "id" then
        return false
    end
    if qualifierWords[text] or text == "layout" or text == "struct" or text == "precision" then
        return true
    end
    if not self:isTypeName(text) then
        return false
    end
    -- "vec3 a", "float[3] a" (not "vec3(...)", "float[3](...)")
    local k = 1
    if self:peek(k) == "[" then
        local depth = 0
        repeat
            local t = self:peek(k)
            if t == nil then
                return false
            elseif t == "[" then
                depth = depth + 1
            elseif t == "]" then
                depth = depth - 1
            end
            k = k + 1
        until depth == 0
    end
    local _, nextKind = self:peek(k)
    return nextKind == "id"
end

-- After the type: "a = 1, b[2], c;" (the ";" too)
function Parser:declarators(t, arrayType)
    repeat
        local name = self:next()
        local isArray = arrayType
        if self:peek() == "[" then
            self:skipBrackets()
            isArray = true
        end
        local vt = t and T(t.name, isArray)
        if self:accept("=") then
            local init = self:assignment()
            if vt and not vt.array then
                self:convertTo(init, vt)
            end
        end
        -- declared after its initializer: "float x = x;" reads an outer x
        self:declare(name, vt)
    until not self:accept(",")
    self:expect(";")
end

-- A declaration in a statement or at the top; at top level it may be a function
function Parser:declaration(topLevel)
    if self:peek() == "precision" then
        while self:next() ~= ";" do end
        return
    end
    local qualified = self:qualifiers()
    if self:peek() == ";" then
        -- "invariant gl_Position;" and such are already gone; a lone qualifier
        self:next()
        return
    end
    -- interface blocks: "uniform Name { ... } instance;"
    local text, kind = self:peek()
    if qualified and kind == "id" and not self:isTypeName(text) and self:peek(1) == "{" then
        self:next()
        self:expect("{")
        local members = {}
        while not self:accept("}") do
            self:qualifiers()
            local t = self:typeSpecifier()
            repeat
                local field = self:next()
                local isArray = false
                if self:peek() == "[" then
                    self:skipBrackets()
                    isArray = true
                end
                members[field] = t and T(t.name, isArray)
                -- members without an instance name are globals
                self:declare(field, members[field])
            until not self:accept(",")
            self:expect(";")
        end
        if self:peek() ~= ";" then
            local instance = self:next()
            self.structs["<block " .. instance .. ">"] = members
            self:declare(instance, T("<block " .. instance .. ">"))
            if self:peek() == "[" then
                self:skipBrackets()
            end
        end
        self:expect(";")
        return
    end

    local t, isStruct = self:typeSpecifier()
    local arrayType = false
    if self:peek() == "[" then
        self:skipBrackets()
        arrayType = true
    end
    if isStruct and self:accept(";") then
        return
    end
    -- a function
    if topLevel and self:peek(1) == "(" then
        local name = self:next()
        self:next()
        local params, names = {}, {}
        if not (self:peek() == ")" or (self:peek() == "void" and self:peek(1) == ")")) then
            repeat
                self:qualifiers()
                local pt = self:typeSpecifier()
                local isArray = false
                if self:peek() == "[" then
                    self:skipBrackets()
                    isArray = true
                end
                local pname
                if self:peek() ~= "," and self:peek() ~= ")" then
                    pname = self:next()
                    if self:peek() == "[" then
                        self:skipBrackets()
                        isArray = true
                    end
                end
                params[#params + 1] = pt and T(pt.name, isArray)
                names[#names + 1] = pname or false
            until not self:accept(",")
        elseif self:peek() == "void" then
            self:next()
        end
        self:expect(")")
        local f = { ret = t and T(t.name, arrayType), params = params }
        -- a prototype and its definition are one function
        local list = self.functions[name] or {}
        local known = false
        for _, g in ipairs(list) do
            if #g.params == #params then
                local same = true
                for i, p in ipairs(params) do
                    if not sameType(p, g.params[i]) then
                        same = false
                    end
                end
                if same then
                    known = true
                end
            end
        end
        if not known then
            list[#list + 1] = f
        end
        self.functions[name] = list
        if self:accept(";") then
            return
        end
        self:push()
        for i, pname in ipairs(names) do
            if pname then
                self:declare(pname, params[i])
            end
        end
        local outer = self.returnType
        self.returnType = f.ret
        self:block(false)
        self.returnType = outer
        self:pop()
        return
    end
    self:declarators(t, arrayType)
end

function Parser:block(newScope)
    self:expect("{")
    if newScope ~= false then
        self:push()
    end
    while not self:accept("}") do
        self:statement()
    end
    if newScope ~= false then
        self:pop()
    end
end

function Parser:statement()
    local text = self:peek()
    if text == "{" then
        self:block()
    elseif text == ";" then
        self:next()
    elseif text == "if" then
        self:next()
        self:expect("(")
        self:expression()
        self:expect(")")
        self:statement()
        if self:accept("else") then
            self:statement()
        end
    elseif text == "for" then
        self:next()
        self:expect("(")
        self:push()
        if self:declarationAhead() then
            self:declaration(false)
        elseif not self:accept(";") then
            self:expression()
            self:expect(";")
        end
        if self:peek() ~= ";" then
            self:expression()
        end
        self:expect(";")
        if self:peek() ~= ")" then
            self:expression()
        end
        self:expect(")")
        self:statement()
        self:pop()
    elseif text == "while" then
        self:next()
        self:expect("(")
        self:expression()
        self:expect(")")
        self:statement()
    elseif text == "do" then
        self:next()
        self:statement()
        self:expect("while")
        self:expect("(")
        self:expression()
        self:expect(")")
        self:expect(";")
    elseif text == "switch" then
        self:next()
        self:expect("(")
        self:expression()
        self:expect(")")
        self:expect("{")
        self:push()
        while not self:accept("}") do
            if self:accept("case") then
                self:expression()
                self:expect(":")
            elseif self:accept("default") then
                self:expect(":")
            else
                self:statement()
            end
        end
        self:pop()
    elseif text == "return" then
        self:next()
        if not self:accept(";") then
            local value = self:expression()
            if self.returnType then
                self:convertTo(value, self.returnType)
            end
            self:expect(";")
        end
    elseif text == "break" or text == "continue" or text == "discard" then
        self:next()
        self:expect(";")
    elseif self:declarationAhead() then
        self:declaration(false)
    else
        self:expression()
        self:expect(";")
    end
end

------------------------------------------------------------------------------

-- source: GLSL after glsl.lua's first steps; version: the source's #version
-- (it was taken out). Returns the source with the conversions written out and
-- how many there were, or the source unchanged, nil and why.
function types.convert(source, version, predefined)
    local tokens = tokenize(source)
    local defines = { GL_ES = { body = "1" } }
    for name, value in pairs(predefined or {}) do
        defines[name] = { body = tostring(value) }
    end
    local ok, why = preprocess(tokens, defines)
    if not ok then
        return source, nil, why
    end

    -- The parse sees the active code; macros are kept as they were written
    local p = setmetatable({
        tokens = tokens, code = {}, pos = 1, scopes = { {} }, structs = {}, functions = {},
        defines = {}, macroTypes = {}, version = version or 110,
        prefix = {}, suffix = {}, replace = {}, count = 0,
    }, Parser)
    for i, t in ipairs(tokens) do
        if t.active and (t.kind == "id" or t.kind == "num" or t.kind == "op") then
            p.code[#p.code + 1] = i
        end
    end
    -- macros as of the end (a macro redefined midway is rare in SMBX2's shaders)
    p.defines = defines

    -- function-like macros used in code would need expanding: leave such shaders
    for _, i in ipairs(p.code) do
        local d = defines[tokens[i].text]
        if tokens[i].kind == "id" and d and d.params then
            return source, nil, "uses the function-like macro " .. tokens[i].text
        end
    end

    local parsed, err = pcall(function()
        while p.pos <= #p.code do
            p:declaration(true)
        end
    end)
    if not parsed then
        local message = type(err) == "table" and err.glsltypes or tostring(err)
        local at = p.code[math.min(p.pos, #p.code)]
        local line = 1
        for i = 1, (at or 0) - 1 do
            local _, nl = string.gsub(tokens[i].text, "\n", "")
            line = line + nl
        end
        return source, nil, message .. " (line " .. line .. ")"
    end

    if p.count == 0 then
        return source, 0
    end
    local out = {}
    for i, t in ipairs(tokens) do
        out[#out + 1] = p.prefix[i] or ""
        out[#out + 1] = p.replace[i] or t.text
        out[#out + 1] = p.suffix[i] or ""
    end
    return table.concat(out), p.count
end

types.tokenize = tokenize -- for tests
types.evalIf = evalIf

return types
