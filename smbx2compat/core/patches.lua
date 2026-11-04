--[[
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
    },
}
