--[[
  The text to find below is quoted from SMBX2's scripts, by the SMBX2 team
  (https://codehaus.moe); credit to them.

  Small source edits to SMBX2 files, applied in memory as boot.lua loads them
  (the files on disk stay untouched). Keys are paths under scripts/, lowercase.
  Each entry is { exact text to find, replacement, why }; the first match is
  replaced, and a warning is logged if the text isn't there (SMBX2 version drift).
  Prefer this to a whole-file override when the change is a line or two.

  Example:
    ["base/engine/require.lua"] = {
        { "old text", "new text", "why" },
    },
]]

return {
    -- LunaLua falls back to its default loading screen without a word when the
    -- episode's loadscreen.lua fails; log why (xtech_lua_loadscreen.cpp)
    ["base/engine/loadscreen.lua"] = {
        {
            "\tif pcall(function() customRequire(\"loadscreen\") end) then",
            "\tlocal ok, err = pcall(function() customRequire(\"loadscreen\") end)\n" ..
            "\tif not ok and not string.find(tostring(err), \"module 'loadscreen' not found\", 1, true) then\n" ..
            "\t\t__smbx2core.warn(\"loading screen: the episode's loadscreen.lua: \" .. tostring(err))\n" ..
            "\tend\n" ..
            "\tif ok then",
            "log why the episode's loading screen failed",
        },
        {
            -- SMBX2's default loader (the colorful X2) goes in the bottom-right
            -- corner of LunaLua's 800x600 screen; TheXTech's screen follows the
            -- window, so take its size (TheXTech's title screen, episodes
            -- without a loadscreen.lua)
            "\t\t\tGraphics.glDraw{vertexCoords={800-imgw,600-imgh,800,600-imgh,800-imgw,600,800,600},",
            "\t\t\tlocal sw, sh = __smbx2core.screenSize()\n" ..
            "\t\t\tGraphics.glDraw{vertexCoords={sw-imgw,sh-imgh,sw,sh-imgh,sw-imgw,sh,sw,sh},",
            "the default loader in the screen's corner, at any screen size",
        },
    },

    -- Shader preprocessor results kept for the session (lunadll.lua's
    -- core.cachedShaderPreprocessor), instead of only for the level's Lua state
    ["base/engine/ffi_graphics.lua"] = {
        {
            "\t\t\t\treturn shader_preprocessor.preprocess(loadShaderFile(input));",
            "\t\t\t\tif shader_preprocessor.onInclude then\n" ..
            "\t\t\t\t\tshader_preprocessor.onInclude(initial, input)\n" ..
            "\t\t\t\tend\n" ..
            "\t\t\t\treturn shader_preprocessor.preprocess(loadShaderFile(input));",
            "report each #include, so kept results can be checked against the files",
        },
        {
            "\tlocal shaderCache = {}",
            "\tshader_preprocessor.execute = __smbx2core.cachedShaderPreprocessor(shader_preprocessor)\n" ..
            "\tlocal shaderCache = {}",
            "keep preprocessed shaders across levels",
        },
    },

    -- Block queries through the engine's block quadtree (xtech_block_queryIntersecting).
    -- SMBX2 scans the blocks SMBX's X-sorted column lookup points at, which TheXTech
    -- doesn't keep, so it would scan every block in the level each call.
    ["base/engine/ffi_block.lua"] = {
        {
            "\tfunction Block.iterateIntersecting(x1, y1, x2, y2)\n" ..
            "\t\tlocal minIdx, maxIdx = lookupMinMax(x1, x2)\n" ..
            "\t\tlocal blockiterator = {x1, y1, x2, y2, maxIdx}\n" ..
            "\t\treturn iterateintersecting, blockiterator, minIdx\n" ..
            "\tend",
            -- Same loop values as SMBX2's: index + 1, then the block
            "\tfunction Block.iterateIntersecting(x1, y1, x2, y2)\n" ..
            "\t\tlocal list, n = xtech_block_queryIntersecting(x1, y1, x2, y2), 0\n" ..
            "\t\treturn function()\n" ..
            "\t\t\tn = n + 1\n" ..
            "\t\t\tlocal idx = list[n]\n" ..
            "\t\t\tif idx then\n" ..
            "\t\t\t\treturn idx + 1, Block(idx)\n" ..
            "\t\t\tend\n" ..
            "\t\tend\n" ..
            "\tend",
            "Block.iterateIntersecting through the block quadtree",
        },
        {
            "\tlocal ret = {}\n" ..
            "\tlocal minIdx, maxIdx = lookupMinMax(x1, x2)\n",
            -- Fills ret and leaves SMBX2's own loop with nothing to do
            "\tlocal ret = {}\n" ..
            "\tfor _, idx in ipairs(xtech_block_queryIntersecting(x1, y1, x2, y2)) do\n" ..
            "\t\tret[#ret + 1] = Block(idx)\n" ..
            "\tend\n" ..
            "\tlocal minIdx, maxIdx = 1, 0\n",
            "Block.getIntersecting through the block quadtree",
        },
        -- Block.iterate(id or IDs) and iterateByFilterMap: the engine finds the next
        -- block with a matching ID (__xtech_ffiBlockNext). SMBX2 reads every block's
        -- ID through mem(), ~14,000 per loop in SMATRS 1-1.
        {
            "\tlocal function iteratewithfilter(tbl, i)\n" ..
            "\t\tlocal count = BlockCount()\n" ..
            "\t\twhile(i < count) do\n" ..
            "\t\t\ti = i + 1\n" ..
            "\t\t\tif tbl[readmem(GM_BLOCK_ADDR + 0x1E + (0x68 * i), FIELD_WORD)] then\n" ..
            "\t\t\t\treturn i, Block(i)\n" ..
            "\t\t\tend\n" ..
            "\t\tend\n" ..
            "\t end\n" ..
            "\t \n" ..
            "\t local function iteratewithid(id, i)\n" ..
            "\t\tlocal count = BlockCount()\n" ..
            "\t\twhile(i < count) do\n" ..
            "\t\t\ti = i + 1\n" ..
            "\t\t\tif readmem(GM_BLOCK_ADDR + 0x1E + (0x68 * i), FIELD_WORD) == id then\n" ..
            "\t\t\t\treturn i, Block(i)\n" ..
            "\t\t\tend\n" ..
            "\t\tend\n" ..
            "\t end\n",
            -- Same loop values as SMBX2's: the block's index, then the block
            "\tlocal blockNext = ffi.cast(\"int32_t (*)(int32_t, const uint8_t *, int32_t)\", __xtech_ffiBlockNext)\n" ..
            "\tlocal idFilter, idFilterSize = __smbx2core.idFilter, __smbx2core.idFilterSize\n" ..
            "\tlocal function iteratewithfilter(tbl, i)\n" ..
            "\t\ti = blockNext(i, idFilter(tbl, i), idFilterSize)\n" ..
            "\t\tif i ~= 0 then\n" ..
            "\t\t\treturn i, Block(i)\n" ..
            "\t\tend\n" ..
            "\tend\n" ..
            "\tlocal function iteratewithid(id, i)\n" ..
            "\t\tif id % 1 ~= 0 then\n" ..
            "\t\t\treturn\n" ..
            "\t\tend\n" ..
            "\t\ti = blockNext(i, nil, id)\n" ..
            "\t\tif i ~= 0 then\n" ..
            "\t\t\treturn i, Block(i)\n" ..
            "\t\tend\n" ..
            "\tend\n",
            "Block.iterate/iterateByFilterMap through the engine",
        },
    },

    -- The same for BGOs (__xtech_ffiBgoNext, xtech_bgo_queryIntersecting). SMBX2's
    -- BGO indices start at 0, the engine's at 1.
    ["base/engine/ffi_bgo.lua"] = {
        -- The count, which every BGO field read checks the BGO against: one call
        -- instead of two mem() reads, a third of all of them in SMATRS 1-1
        {
            "local function bgoGetCount()\n" ..
            "\treturn readmem(GM_BGO_COUNT_ADDR, FIELD_WORD) + readmem(GM_BGO_LOCK_COUNT_ADDR, FIELD_WORD)\n" ..
            "end\n",
            "local engineBGOCount = ffi.cast(\"int32_t (*)()\", __xtech_ffiBgoCount)\n" ..
            "local function bgoGetCount()\n" ..
            "\tlocal n = engineBGOCount()\n" ..
            "\treturn n\n" ..
            "end\n",
            "BGO count from the engine",
        },
        {
            "\tlocal ret = {}\n" ..
            "\tlocal ptr = GM_BGO_ADDR\n" ..
            "\tfor idx=0,bgoGetCount()-1 do\n",
            -- Fills ret and leaves SMBX2's own loop with nothing to do
            "\tlocal ret = {}\n" ..
            "\tfor _, idx in ipairs(xtech_bgo_queryIntersecting(x1, y1, x2, y2)) do\n" ..
            "\t\tret[#ret + 1] = BGO(idx)\n" ..
            "\tend\n" ..
            "\tlocal ptr = GM_BGO_ADDR\n" ..
            "\tfor idx=0,-1 do\n",
            "BGO.getIntersecting through the BGO quadtree",
        },
        {
            " local function iteratewithid(id, i)\n" ..
            "\tlocal count = bgoGetCount()\n" ..
            "\twhile(i < count) do\n" ..
            "\t\tif readmem(GM_BGO_ADDR + 0x06 + (BGO_STRUCT_SIZE * i), FIELD_WORD) == id then\n" ..
            "\t\t\treturn i + 1, BGO(i)\n" ..
            "\t\tend\n" ..
            "\t\ti = i + 1\n" ..
            "\tend\n" ..
            " end\n",
            -- Same loop values as SMBX2's: the BGO's index + 1, then the BGO
            "local bgoNext = ffi.cast(\"int32_t (*)(int32_t, const uint8_t *, int32_t)\", __xtech_ffiBgoNext)\n" ..
            "local idFilter, idFilterSize = __smbx2core.idFilter, __smbx2core.idFilterSize\n" ..
            "local function iteratewithid(id, i)\n" ..
            "\tif id % 1 ~= 0 then\n" ..
            "\t\treturn\n" ..
            "\tend\n" ..
            "\ti = bgoNext(i, nil, id)\n" ..
            "\tif i ~= 0 then\n" ..
            "\t\treturn i, BGO(i - 1)\n" ..
            "\tend\n" ..
            "end\n",
            "BGO.iterate(id) through the engine",
        },
        {
            "local function iteratewithfilter(tbl, i)\n" ..
            "\tlocal count = bgoGetCount()\n" ..
            "\twhile(i < count) do\n" ..
            "\t\tif tbl[readmem(GM_BGO_ADDR + 0x06 + (BGO_STRUCT_SIZE * i), FIELD_WORD)] then\n" ..
            "\t\t\treturn i + 1, BGO(i)\n" ..
            "\t\tend\n" ..
            "\t\ti = i + 1\n" ..
            "\tend\n" ..
            " end\n",
            "local function iteratewithfilter(tbl, i)\n" ..
            "\ti = bgoNext(i, idFilter(tbl, i), idFilterSize)\n" ..
            "\tif i ~= 0 then\n" ..
            "\t\treturn i, BGO(i - 1)\n" ..
            "\tend\n" ..
            "end\n",
            "BGO.iterate(IDs)/iterateByFilterMap through the engine",
        },
    },
}
