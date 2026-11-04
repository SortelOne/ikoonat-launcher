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
}
