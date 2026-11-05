/*
 * TheXTech - A platform game engine ported from old source code for VB6
 *
 * Copyright (c) 2009-2011 Andrew Spinks, original VB6 code
 * Copyright (c) 2020-2026 Vitaly Novichkov <admin@wohlnet.ru>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once
#ifndef RENDER_SCRIPT_H
#define RENDER_SCRIPT_H

#include <string>
#include <vector>

struct StdPicture;

// smbx2compat: SMBX2 (LunaLua) draws that need their own OpenGL state: shader
// programs, render targets ("capture buffers") and LunaLua's premultiplied
// blending. The renderer draws its queued draws first, then draws these
// immediately (AbstractRender_t::scriptDraw). OpenGL renderer only.

//! A texture for a script draw: a picture, a capture buffer, or neither
struct ScriptTexture_t
{
    StdPicture *picture = nullptr;
    int buffer = 0;
};

//! A uniform or attribute of a script shader program (as glGetActive* reports it)
struct ScriptShaderVar_t
{
    std::string name;
    int location = -1;
    unsigned type = 0;
    int size = 1;
};

//! A value for a uniform (count elements) or attribute (one element per vertex).
//! Samplers: data is count ScriptTexture_t.
struct ScriptDrawVar_t
{
    int location = -1;
    unsigned type = 0;
    int count = 0;
    const void *data = nullptr;
};

struct ScriptDraw_t
{
    //! capture buffer to draw into; 0: the screen
    int target = 0;
    //! iChannel0
    ScriptTexture_t texture;
    //! program from scriptShaderCreate
    int shader = 0;
    //! GL primitive (GL_TRIANGLES, GL_LINES, ...)
    unsigned primitive = 0;
    int count = 0;
    //! 2 floats per vertex; uv: 0..1 of the texture, or nullptr
    const float *xy = nullptr;
    const float *uv = nullptr;
    //! 4 floats per vertex, premultiplied, or nullptr (then color is used)
    const float *rgba = nullptr;
    //! straight alpha
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    //! added to every vertex (scene coordinates -> the camera's view)
    float offsetX = 0.0f, offsetY = 0.0f;
    bool linearFiltered = false;
    //! test and write depth (LEQUAL), in a capture buffer's own depth buffer
    bool depthTest = false;
    //! pixels where a*x + b*y + c*0 + d < 0 for any plane (x, y in the view,
    //! after offsetX/Y) aren't drawn (LunaLua's glClipPlane)
    int clipPlaneCount = 0;
    float clipPlanes[6][4] = {};

    const ScriptDrawVar_t *attributes = nullptr;
    int attributeCount = 0;
    const ScriptDrawVar_t *uniforms = nullptr;
    int uniformCount = 0;
};

#endif // RENDER_SCRIPT_H
