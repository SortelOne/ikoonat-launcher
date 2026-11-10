-- A small C-style preprocessor for the harness, as SMBX2's (floats in #if,
-- #include "file" or #include MACRO, object- and function-like macros)
local types = ...
local tokenize, evalIf = types.tokenize, types.evalIf

local function readFile(path)
    local f = io.open(path, "rb")
    if not f then return nil end
    local s = f:read("*a")
    f:close()
    return s
end

local function exists(path)
    local f = io.open(path, "rb")
    if f then f:close() return true end
    return false
end

-- expands macros in a list of tokens (space kept), returns text
local function expand(toks, defines, disabled, depth)
    depth = depth or 0
    if depth > 50 then error("macro recursion") end
    local out = {}
    local i = 1
    while i <= #toks do
        local t = toks[i]
        local d = t.kind == "id" and defines[t.text]
        if d and not disabled[t.text] then
            if not d.params then
                local inner = tokenize(d.body)
                disabled[t.text] = true
                out[#out + 1] = expand(inner, defines, disabled, depth + 1)
                disabled[t.text] = nil
                i = i + 1
            else
                -- find "(" after spaces
                local j = i + 1
                while toks[j] and toks[j].kind == "space" do j = j + 1 end
                if not toks[j] or toks[j].text ~= "(" then
                    out[#out + 1] = t.text
                    i = i + 1
                else
                    local args, cur, level = {}, {}, 0
                    j = j + 1
                    while true do
                        local a = toks[j]
                        if not a then error("unterminated macro call " .. t.text) end
                        if a.text == "(" then level = level + 1 end
                        if a.text == ")" then
                            if level == 0 then break end
                            level = level - 1
                        end
                        if a.text == "," and level == 0 then
                            args[#args + 1] = cur
                            cur = {}
                        else
                            cur[#cur + 1] = a
                        end
                        j = j + 1
                    end
                    args[#args + 1] = cur
                    local map = {}
                    for k, p in ipairs(d.params) do
                        map[p] = expand(args[k] or {}, defines, disabled, depth + 1)
                    end
                    local body = tokenize(d.body)
                    local subst = {}
                    for _, b in ipairs(body) do
                        if b.kind == "id" and map[b.text] then
                            subst[#subst + 1] = map[b.text]
                        else
                            subst[#subst + 1] = b.text
                        end
                    end
                    disabled[t.text] = true
                    out[#out + 1] = expand(tokenize(table.concat(subst)), defines, disabled, depth + 1)
                    disabled[t.text] = nil
                    i = j + 1
                end
            end
        else
            out[#out + 1] = t.text
            i = i + 1
        end
    end
    return table.concat(out)
end

local function process(path, defines, incDirs, out)
    local text = readFile(path)
    if not text then error("missing " .. path) end
    text = text:gsub("#[ \t]*version[^\n]*", ""):gsub("#[ \t]*extension[^\n]*", "")
    local dir = path:match("^(.*)/")
    local toks = tokenize(text)
    local stack, active = {}, true
    local pending = {}
    local function flush()
        if #pending > 0 then
            out[#out + 1] = expand(pending, defines, {})
            pending = {}
        end
    end
    for _, t in ipairs(toks) do
        if t.kind == "pp" then
            flush()
            local line = t.text:gsub("\\\r?\n", " ")
            local directive, rest = line:match("^#%s*([%a_]*)%s*(.-)%s*$")
            if directive == "include" and active then
                local file = rest:match('^"(.-)"$')
                if not file then
                    local d = defines[rest]
                    file = d and d.body:match('^"(.-)"$')
                end
                if not file then error("bad include " .. rest) end
                local found
                local bases = { dir }
                for _, b in ipairs(incDirs) do bases[#bases + 1] = b end
                for _, base in ipairs(bases) do
                    if exists(base .. "/" .. file) then
                        found = base .. "/" .. file
                        break
                    end
                end
                if not found then error("include not found: " .. file) end
                process(found, defines, incDirs, out)
            elseif directive == "define" and active and defines[rest:match("^[%a_][%w_]*") or ""] and
                   defines[rest:match("^[%a_][%w_]*")].locked then
                -- the harness's own value wins (as SMATRS's map.lua sets them)
            elseif directive == "define" and active then
                local name, params, body = rest:match("^([%a_][%w_]*)%(([^)]*)%)%s*(.*)$")
                if name then
                    local list = {}
                    for p in params:gmatch("[%a_][%w_]*") do list[#list + 1] = p end
                    defines[name] = { params = list, body = body }
                else
                    name, body = rest:match("^([%a_][%w_]*)%s*(.*)$")
                    defines[name] = { body = body }
                end
            elseif directive == "undef" and active then
                defines[rest:match("^[%a_][%w_]*")] = nil
            elseif directive == "ifdef" or directive == "ifndef" then
                local cond = (defines[rest:match("^[%a_][%w_]*")] ~= nil) == (directive == "ifdef")
                stack[#stack + 1] = { parent = active, taken = cond }
                active = active and cond
            elseif directive == "if" then
                local v = 0
                if active then
                    v = evalIf(rest, defines)
                    if v == nil then error("can't evaluate #if " .. rest) end
                end
                stack[#stack + 1] = { parent = active, taken = v ~= 0 }
                active = active and v ~= 0
            elseif directive == "elif" then
                local top = stack[#stack]
                if top.taken or not top.parent then
                    active = false
                else
                    local v = evalIf(rest, defines)
                    if v == nil then error("can't evaluate #elif " .. rest) end
                    top.taken = v ~= 0
                    active = v ~= 0
                end
            elseif directive == "else" then
                local top = stack[#stack]
                active = top.parent and not top.taken
                top.taken = true
            elseif directive == "endif" then
                active = table.remove(stack).parent
            end
            out[#out + 1] = "\n"
        elseif active then
            pending[#pending + 1] = t
        elseif t.kind == "space" then
            local _, n = t.text:gsub("\n", "")
            out[#out + 1] = string.rep("\n", n)
        end
    end
    flush()
end

return function(path, defines, incDirs)
    local d = {}
    for k, v in pairs(defines or {}) do d[k] = { body = v, locked = true } end
    local out = {}
    local ok, err = pcall(process, path, d, incDirs, out)
    if not ok then return nil, tostring(err) end
    return table.concat(out)
end
