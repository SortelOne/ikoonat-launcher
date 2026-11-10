# Shader translation checks

`esconvert.lua` runs `core/glsl.lua`'s OpenGL ES translation over SMBX2's
and SMATRS's shaders and compiles each with `glslc` (glslang, the same type
rules and messages as Adreno's compiler). It compares the translation with
`core/glsltypes.lua` (int -> float conversions written out) against the one
without, as a GPU without `GL_EXT_shader_implicit_conversions` sees them
(the Retroid Pocket Nova's Adreno 740 driver).

```bash
~/luajit-host/src/luajit smbx2compat/test/glsl/esconvert.lua           # all
V=1 ~/luajit-host/src/luajit smbx2compat/test/glsl/esconvert.lua lib3d # some, listed
```

Needs the shader files in `GLSL_CORPUS` (default `~/ikoonat-shader-corpus`),
named by their path in the SMATRS bundle with `/` written `__`
(`data__scripts__shaders__darkness__darkness.frag`). They are game data:
keep them out of git. `pp.lua` is the test's stand-in for SMBX2's shader
preprocessor (`#include`, macros, decimals in `#if`).

As of 2026-11-09: 227 shaders, 73 failing without glsltypes.lua, 1 with it
(SMBX2's `map3d/texturetiler.frag`, which SMATRS doesn't use: a global
initialized from a uniform, which ES doesn't allow).
