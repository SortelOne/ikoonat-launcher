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

// smbx2compat: SMBX2's draw queue (LunaLua's Renderer + LuaProxyFFIGraphics.cpp).
//
// smbx2compat/core/lunadll.lua hands SMBX2's FFI_GLDraw, FFI_ImageDraw,
// FFI_TextDraw and LunaLuaAlloc straight to the functions below through LuaJIT's
// FFI (function pointers in __xtech_ffi* globals), so they must never call back
// into Lua. Each draw is queued with its render priority and the camera being
// drawn when it was queued (0: every camera), and drawn when the level renderer
// reaches that priority (xtech_lua_drawBelow). The queue empties every frame.
//
// A LunaImageRef* is a number here: an entry in s_images, whose picture is
// shared by everything that loaded the same file. Pictures load premultiplied,
// as LunaLua's do.
//
// Draws with a shader, into or from a capture buffer, or of points and lines
// go to the renderer's script draws (core/render_script.h), which run their own
// GL program with LunaLua's blending; the rest join the engine's draw queue.
// FFI_ShaderObjRef* and CaptureBufferRef* are handle numbers from lunadll.lua,
// mapped to the renderer's programs and buffers here.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
}

#include <FreeImageLite.h>
#include <Logger/logger.h>
#include <AppPath/app_path.h>

#include "globals.h"
#include "config.h"
#include "graphics.h"
#include "screen.h"
#include "core/render.h"
#include "load_gfx.h"
#include "script/luna/luna.h"

#include "xtech_lua_draw.h"

namespace
{

// ffi_graphics.lua's declarations (the C layout LuaJIT gives them)
struct FFI_GL_Draw_Var
{
    unsigned int mId;
    unsigned int mType;
    unsigned int mCount;
    void *mData;
};

struct FFI_GL_Draw_Cmd
{
    void *mTarget;
    void *mImg;
    void *mCap;
    float mColor[4];
    uint32_t mType;
    uint32_t mCount;
    bool mSceneCoords;
    bool mDepthTest;
    bool mLinearFiltered;
    const float *mVert;
    const float *mTex;
    const float *mVertColor;
    void *mShader;
    unsigned int mAttrCount;
    FFI_GL_Draw_Var *mAttrs;
    unsigned int mUnifCount;
    FFI_GL_Draw_Var *mUnifs;
    double mPriority;
    uint8_t mNumClipPlane;
    double mClipPlane[6][4];
};

enum GLPrimitive
{
    GL_PRIM_TRIANGLES = 4,
    GL_PRIM_TRIANGLE_STRIP = 5,
    GL_PRIM_TRIANGLE_FAN = 6,
};

// GL types of shader variables, as ffi_graphics packs them
enum GLVarType : unsigned
{
    GLT_FLOAT = 0x1406, GLT_VEC2 = 0x8B50, GLT_VEC3 = 0x8B51, GLT_VEC4 = 0x8B52,
    GLT_INT = 0x1404, GLT_IVEC2 = 0x8B53, GLT_IVEC3 = 0x8B54, GLT_IVEC4 = 0x8B55,
    GLT_UINT = 0x1405, GLT_UVEC2 = 0x8DC6, GLT_UVEC3 = 0x8DC7, GLT_UVEC4 = 0x8DC8,
    GLT_MAT2 = 0x8B5A, GLT_MAT3 = 0x8B5B, GLT_MAT4 = 0x8B5C,
    GLT_MAT2x3 = 0x8B65, GLT_MAT2x4 = 0x8B66, GLT_MAT3x2 = 0x8B67,
    GLT_MAT3x4 = 0x8B68, GLT_MAT4x2 = 0x8B69, GLT_MAT4x3 = 0x8B6A,
    GLT_SAMPLER_2D = 0x8B5E,
};

// 4-byte values per element of a type (floats, ints); 0: not supported
int varWords(unsigned type)
{
    switch(type)
    {
    case GLT_FLOAT: case GLT_INT: case GLT_UINT:     return 1;
    case GLT_VEC2: case GLT_IVEC2: case GLT_UVEC2:   return 2;
    case GLT_VEC3: case GLT_IVEC3: case GLT_UVEC3:   return 3;
    case GLT_VEC4: case GLT_IVEC4: case GLT_UVEC4:   return 4;
    case GLT_MAT2:                                   return 4;
    case GLT_MAT3:                                   return 9;
    case GLT_MAT4:                                   return 16;
    case GLT_MAT2x3: case GLT_MAT3x2:                return 6;
    case GLT_MAT2x4: case GLT_MAT4x2:                return 8;
    case GLT_MAT3x4: case GLT_MAT4x3:                return 12;
    default:                                         return 0;
    }
}

// Pictures by file, kept until the level's Lua state closes
std::unordered_map<std::string, std::unique_ptr<StdPicture>> s_pictures;
// LunaImageRef numbers -> pictures
std::unordered_map<uintptr_t, StdPicture *> s_images;
uintptr_t s_nextImage = 1;

bool s_vanilla[XTECH_LUA_DRAW_COUNT] = {true, true, true, true};
bool s_hudSkipped = false;

// Handle numbers from lunadll.lua -> the renderer's programs and capture buffers
std::unordered_map<uintptr_t, int> s_shaders;
std::unordered_map<uintptr_t, int> s_buffers;
// SMBX2's standard.vert/frag, for script draws without a shader (0: the
// renderer has no script draws)
int s_standardShader = 0;
// freed after this frame's draws, which may still use them
std::vector<int> s_freedShaders, s_freedBuffers;

// FFI_GraphicsSetMainFramebufferSize's size, which the engine applies later
int s_fbW = 0, s_fbH = 0;

enum OpKind : uint8_t
{
    OP_TRIANGLES,
    OP_IMAGE,
    OP_TEXT,
    OP_SCRIPT,   // first = index into s_scriptOps
    OP_CAPTURE,  // first = capture buffer (renderer id)
    OP_CLEAR,    // first = capture buffer (renderer id)
    OP_REDIRECT,   // first = capture buffer (renderer id), count = redirect handle
    OP_UNREDIRECT, // count = handle of its OP_REDIRECT
};

struct DrawOp
{
    double priority;
    int camera;          // 0: every camera
    unsigned drawnPass;  // last camera pass that drew it
    OpKind kind;
    bool scene;          // level coordinates (else screen coordinates)
    StdPicture *picture;
    XTColor color;
    // OP_TRIANGLES: vertices [first, first + count) of s_vertices
    // OP_IMAGE: x, y = position; u, v = source position; count/first = source size
    // OP_TEXT: x, y = position; first = index into s_texts, count = font
    // OP_SCRIPT, OP_CAPTURE, OP_CLEAR, OP_(UN)REDIRECT: see OpKind
    double x, y;
    int u, v;
    uint32_t first, count;
};

struct DrawVertex
{
    float x, y;
    float u, v;
    float r, g, b, a;
};

std::vector<DrawOp> s_ops;
std::vector<DrawVertex> s_vertices;
std::vector<std::string> s_texts;

// A draw for the renderer's script draws (ScriptDraw_t, with offsets for pointers)
constexpr uint32_t c_none = UINT32_MAX;

struct ScriptOp
{
    int target;
    ScriptTexture_t texture;
    int shader;
    uint32_t primitive;
    uint32_t count;
    uint32_t xy, uv, rgba;  // offsets into s_floats (c_none: absent)
    float color[4];
    bool linear;
    bool depthTest;
    uint32_t varFirst;      // s_vars: attributes, then uniforms
    uint32_t attrCount, unifCount;
};

struct QueuedVar
{
    int location;
    unsigned type;
    int count;
    size_t offset;          // into s_varData, in bytes
};

std::vector<ScriptOp> s_scriptOps;
std::vector<float> s_floats;
std::vector<QueuedVar> s_vars;
std::vector<uint64_t> s_varData;   // 8-byte aligned (samplers hold pointers)
std::vector<ScriptDrawVar_t> s_drawVars;

// drawing order: indices into s_ops sorted by priority (queue order on ties)
std::vector<uint32_t> s_order;
size_t s_orderCursor = 0;

// Graphics.redirectCameraFB's active redirects, innermost last (LunaLua's
// GLContextManager::mActiveRedirects): {handle, capture buffer}
std::vector<std::pair<uint32_t, int>> s_redirects;
uint32_t s_nextRedirect = 0;

int s_curCamera = 0;       // camera whose draws are being queued (0: all)
int s_drawScreen = 0;      // vScreen being drawn (0: none)
unsigned s_pass = 0;       // counts camera passes

// scratch arrays for XRender::renderTriangles
std::vector<float> s_xy, s_uv, s_rgba;

// testing: a screenshot of this frame is due (xtech_gfx_screenshot)
bool s_shotDue = false;
int s_shotCount = 0;

void warnOnce(bool &warned, const char *what)
{
    if(!warned)
    {
        warned = true;
        pLogWarning("smbx2compat: %s", what);
    }
}

StdPicture *pictureFor(const void *ref)
{
    if(!ref)
        return nullptr;

    auto it = s_images.find(reinterpret_cast<uintptr_t>(ref));
    return it != s_images.end() ? it->second : nullptr;
}

// Draws wait for the next rendered frame; if the engine stops rendering for a
// long time (frameskip, a hidden window) the oldest are dropped
constexpr size_t c_maxQueuedOps = 65536;
constexpr size_t c_maxQueuedVertices = 4u << 20;

void makeRoom()
{
    if(s_ops.size() >= c_maxQueuedOps || s_vertices.size() >= c_maxQueuedVertices
       || s_floats.size() >= 8 * c_maxQueuedVertices || s_varData.size() >= c_maxQueuedVertices)
    {
        static bool warned = false;
        warnOnce(warned, "SMBX2 draw queue overflowed without a rendered frame (dropped)");
        xtech_lua_drawFrameEnd();
    }
}

DrawOp &newOp(OpKind kind, double priority, bool scene)
{
    s_ops.emplace_back();
    DrawOp &op = s_ops.back();
    op.priority = priority;
    op.camera = s_curCamera;
    op.drawnPass = 0;
    op.kind = kind;
    op.scene = scene;
    op.picture = nullptr;
    op.color = XTColor();
    op.x = op.y = 0.0;
    op.u = op.v = 0;
    op.first = op.count = 0;
    return op;
}

inline uint8_t toByte(float f)
{
    return (uint8_t)(f <= 0.0f ? 0 : f >= 1.0f ? 255 : f * 255.0f + 0.5f);
}

void pushVertex(const FFI_GL_Draw_Cmd &cmd, uint32_t i, bool premultiplied)
{
    DrawVertex v;
    v.x = cmd.mVert[2 * i];
    v.y = cmd.mVert[2 * i + 1];
    v.u = cmd.mTex ? cmd.mTex[2 * i] : 0.0f;
    v.v = cmd.mTex ? cmd.mTex[2 * i + 1] : 0.0f;

    if(cmd.mVertColor && premultiplied)
    {
        const float *c = cmd.mVertColor + 4 * i;
        v.r = c[0];
        v.g = c[1];
        v.b = c[2];
        v.a = c[3];
    }
    else if(cmd.mVertColor)
    {
        // SMBX2's vertex colours are premultiplied; flat colour blends straight alpha
        // (additive colour, with zero alpha, has no straight-alpha equivalent)
        const float *c = cmd.mVertColor + 4 * i;
        v.a = c[3];
        if(v.a > 0.0f)
        {
            v.r = std::min(c[0] / v.a, 1.0f);
            v.g = std::min(c[1] / v.a, 1.0f);
            v.b = std::min(c[2] / v.a, 1.0f);
        }
        else
            v.r = v.g = v.b = 0.0f;
    }
    else
        v.r = v.g = v.b = v.a = 1.0f;

    s_vertices.push_back(v);
}

// LunaLuaAlloc: arrays ffi_graphics fills for FFI_GLDraw, which frees them
void *ffi_alloc(size_t size)
{
    return std::malloc(size);
}

int shaderFor(const void *ref)
{
    auto it = s_shaders.find(reinterpret_cast<uintptr_t>(ref));
    return it != s_shaders.end() ? it->second : 0;
}

int bufferFor(const void *ref)
{
    if(!ref)
        return 0;

    auto it = s_buffers.find(reinterpret_cast<uintptr_t>(ref));
    return it != s_buffers.end() ? it->second : 0;
}

// Copies a shader variable's value for a script draw; false if it can't be used
bool queueVar(const FFI_GL_Draw_Var &var)
{
    static bool warnedType = false, warnedDepth = false;

    if(!var.mData || var.mCount == 0)
        return false;

    QueuedVar q;
    q.location = (int)var.mId;
    q.type = var.mType;
    q.count = (int)var.mCount;
    q.offset = s_varData.size() * sizeof(uint64_t);

    if(var.mType == GLT_SAMPLER_2D)
    {
        // pairs of (kind, ref): 1 = image, 2 = capture buffer, 3 = depth buffer
        size_t words = (sizeof(ScriptTexture_t) * var.mCount + sizeof(uint64_t) - 1) / sizeof(uint64_t);
        s_varData.resize(s_varData.size() + words);
        ScriptTexture_t *t = reinterpret_cast<ScriptTexture_t *>(s_varData.data() + q.offset / sizeof(uint64_t));
        void *const *pairs = static_cast<void *const *>(var.mData);
        for(unsigned i = 0; i < var.mCount; i++)
        {
            uintptr_t kind = reinterpret_cast<uintptr_t>(pairs[2 * i]);
            t[i] = ScriptTexture_t();
            if(kind == 1)
                t[i].picture = pictureFor(pairs[2 * i + 1]);
            else if(kind == 2)
                t[i].buffer = bufferFor(pairs[2 * i + 1]);
            else if(kind == 3)
                warnOnce(warnedDepth, "shader depth buffers aren't supported yet (sampled as white)");
        }
    }
    else
    {
        int w = varWords(var.mType);
        if(!w)
        {
            warnOnce(warnedType, "a shader variable type isn't supported yet (doubles?) (not set)");
            return false;
        }

        size_t bytes = (size_t)w * 4 * var.mCount;
        size_t words = (bytes + sizeof(uint64_t) - 1) / sizeof(uint64_t);
        s_varData.resize(s_varData.size() + words);
        std::memcpy(s_varData.data() + q.offset / sizeof(uint64_t), var.mData, bytes);
    }

    s_vars.push_back(q);
    return true;
}

// Graphics.glDraw for the renderer's script draws
void queueScriptDraw(const FFI_GL_Draw_Cmd &cmd, int shader)
{
    makeRoom();

    ScriptOp so;
    so.target = bufferFor(cmd.mTarget);
    so.texture.picture = pictureFor(cmd.mImg);
    so.texture.buffer = bufferFor(cmd.mCap);
    so.shader = shader ? shader : s_standardShader;
    so.primitive = cmd.mType;
    so.count = cmd.mCount;
    so.color[0] = cmd.mColor[0];
    so.color[1] = cmd.mColor[1];
    so.color[2] = cmd.mColor[2];
    so.color[3] = cmd.mColor[3];
    so.linear = cmd.mLinearFiltered;
    so.depthTest = cmd.mDepthTest;

    so.xy = (uint32_t)s_floats.size();
    s_floats.insert(s_floats.end(), cmd.mVert, cmd.mVert + 2 * cmd.mCount);
    so.uv = c_none;
    if(cmd.mTex)
    {
        so.uv = (uint32_t)s_floats.size();
        s_floats.insert(s_floats.end(), cmd.mTex, cmd.mTex + 2 * cmd.mCount);
    }
    so.rgba = c_none;
    if(cmd.mVertColor)
    {
        so.rgba = (uint32_t)s_floats.size();
        s_floats.insert(s_floats.end(), cmd.mVertColor, cmd.mVertColor + 4 * cmd.mCount);
    }

    so.varFirst = (uint32_t)s_vars.size();
    so.attrCount = so.unifCount = 0;
    if(shader)
    {
        for(unsigned i = 0; cmd.mAttrs && i < cmd.mAttrCount; i++)
            so.attrCount += queueVar(cmd.mAttrs[i]) ? 1 : 0;
        for(unsigned i = 0; cmd.mUnifs && i < cmd.mUnifCount; i++)
            so.unifCount += queueVar(cmd.mUnifs[i]) ? 1 : 0;
    }

    DrawOp &op = newOp(OP_SCRIPT, cmd.mPriority, cmd.mSceneCoords);
    op.first = (uint32_t)s_scriptOps.size();
    s_scriptOps.push_back(so);
}

void ffi_glDraw(const FFI_GL_Draw_Cmd *cmd)
{
    static bool warnedTarget = false, warnedShader = false, warnedClip = false,
                warnedPrimitive = false, warnedCompile = false;

    if(!cmd)
        return;

    // LunaLua frees these after drawing (a shader's attribute and uniform data too)
    struct Owned
    {
        const FFI_GL_Draw_Cmd *c;
        ~Owned()
        {
            std::free(const_cast<float *>(c->mVert));
            std::free(const_cast<float *>(c->mTex));
            std::free(const_cast<float *>(c->mVertColor));
            for(unsigned i = 0; c->mAttrs && i < c->mAttrCount; i++)
                std::free(c->mAttrs[i].mData);
            for(unsigned i = 0; c->mUnifs && i < c->mUnifCount; i++)
                std::free(c->mUnifs[i].mData);
        }
    } owned{cmd};

    if(cmd->mNumClipPlane)
        warnOnce(warnedClip, "Graphics.glDraw clip planes aren't supported yet (ignored)");

    if(!cmd->mVert || cmd->mCount == 0)
        return;

    // LunaLua multiplies vertex colours by the draw's colour (both premultiplied), which
    // then plays no part: textplus tints whole texts this way (littleDialogue's answers).
    // The arrays are ours (freed above), as LunaLua's are, so this is done in place.
    const float *tint = cmd->mColor;
    if(cmd->mVertColor && (tint[0] != 1.0f || tint[1] != 1.0f || tint[2] != 1.0f || tint[3] != 1.0f))
    {
        float *c = const_cast<float *>(cmd->mVertColor);
        for(uint32_t i = 0; i < cmd->mCount; i++, c += 4)
        {
            c[0] *= tint[0];
            c[1] *= tint[1];
            c[2] *= tint[2];
            c[3] *= tint[3];
        }
    }

    // a shader that didn't compile here (lunadll.lua logged why) draws as without one
    int shader = cmd->mShader ? shaderFor(cmd->mShader) : 0;
    if(cmd->mShader && !shader)
        warnOnce(warnedCompile, "Graphics.glDraw with a shader that didn't compile here (drawn without)");

    bool lines = cmd->mType < GL_PRIM_TRIANGLES;
    if(shader || cmd->mTarget || cmd->mCap || lines)
    {
        if(s_standardShader)
        {
            queueScriptDraw(*cmd, shader);
            return;
        }

        // the renderer can't: draw what the engine can
        if(cmd->mTarget || cmd->mCap)
        {
            warnOnce(warnedTarget, "Graphics.glDraw with capture buffers needs OpenGL 3.3 or ES (skipped)");
            return;
        }
        if(cmd->mShader)
            warnOnce(warnedShader, "Graphics.glDraw shaders need OpenGL 3.3 or ES (drawn without)");
    }

    if(cmd->mCount < 3)
        return;

    makeRoom();
    uint32_t first = (uint32_t)s_vertices.size();
    uint32_t n = cmd->mCount;

    // textured draws keep LunaLua's premultiplied colours (pictures are premultiplied)
    StdPicture *picture = pictureFor(cmd->mImg);
    bool pm = picture != nullptr;

    switch(cmd->mType)
    {
    case GL_PRIM_TRIANGLES:
        for(uint32_t i = 0; i + 2 < n; i += 3)
        {
            pushVertex(*cmd, i, pm);
            pushVertex(*cmd, i + 1, pm);
            pushVertex(*cmd, i + 2, pm);
        }
        break;

    case GL_PRIM_TRIANGLE_STRIP:
        for(uint32_t i = 0; i + 2 < n; i++)
        {
            // keep the winding of every other triangle
            pushVertex(*cmd, (i & 1) ? i + 1 : i, pm);
            pushVertex(*cmd, (i & 1) ? i : i + 1, pm);
            pushVertex(*cmd, i + 2, pm);
        }
        break;

    case GL_PRIM_TRIANGLE_FAN:
        for(uint32_t i = 1; i + 1 < n; i++)
        {
            pushVertex(*cmd, 0, pm);
            pushVertex(*cmd, i, pm);
            pushVertex(*cmd, i + 1, pm);
        }
        break;

    default:
        warnOnce(warnedPrimitive, "Graphics.glDraw points and lines need OpenGL 3.3 or ES (skipped)");
        return;
    }

    DrawOp &op = newOp(OP_TRIANGLES, cmd->mPriority, cmd->mSceneCoords);
    op.picture = picture;
    // vertex colours carry the draw's colour (multiplied in above)
    if(cmd->mVertColor)
        op.color = XTColor();
    else if(pm)
    {
        float a = cmd->mColor[3];
        op.color = XTColor(toByte(cmd->mColor[0] * a), toByte(cmd->mColor[1] * a),
                           toByte(cmd->mColor[2] * a), toByte(a));
    }
    else
    {
        op.color = XTColor(toByte(cmd->mColor[0]), toByte(cmd->mColor[1]),
                           toByte(cmd->mColor[2]), toByte(cmd->mColor[3]));
    }
    op.first = first;
    op.count = (uint32_t)s_vertices.size() - first;
}

void ffi_imageDraw(void *img, double x, double y, double sx, double sy, double sw, double sh,
                   double priority, float opacity, bool sceneCoords)
{
    StdPicture *picture = pictureFor(img);
    if(!picture)
        return;

    makeRoom();
    DrawOp &op = newOp(OP_IMAGE, priority, sceneCoords);
    op.picture = picture;
    // premultiplied white
    op.color = XTColor(toByte(opacity), toByte(opacity), toByte(opacity), toByte(opacity));
    op.x = x;
    op.y = y;
    op.u = (int)std::lround(sx);
    op.v = (int)std::lround(sy);
    op.first = (uint32_t)std::max(0l, std::lround(sw));
    op.count = (uint32_t)std::max(0l, std::lround(sh));
}

void ffi_textDraw(const char *text, int type, int x, int y, double priority, bool sceneCoords)
{
    if(!text)
        return;

    std::string s = text;
    // LunaLua: font 3 has only capitals
    if(type == 3)
    {
        for(char &c : s)
        {
            if(c >= 'a' && c <= 'z')
                c = (char)(c - 'a' + 'A');
        }
    }

    makeRoom();
    DrawOp &op = newOp(OP_TEXT, priority, sceneCoords);
    op.x = x;
    op.y = y;
    op.first = (uint32_t)s_texts.size();
    op.count = (uint32_t)type;
    s_texts.push_back(std::move(s));
}

void drawScriptOp(const ScriptOp &so, double offX, double offY)
{
    ScriptDraw_t d;
    d.target = so.target;
    d.texture = so.texture;
    d.shader = so.shader;
    d.primitive = so.primitive;
    d.count = (int)so.count;
    d.xy = &s_floats[so.xy];
    d.uv = so.uv != c_none ? &s_floats[so.uv] : nullptr;
    d.rgba = so.rgba != c_none ? &s_floats[so.rgba] : nullptr;
    std::memcpy(d.color, so.color, sizeof(d.color));
    d.offsetX = (float)offX;
    d.offsetY = (float)offY;
    d.linearFiltered = so.linear;
    d.depthTest = so.depthTest;

    s_drawVars.clear();
    for(uint32_t i = 0; i < so.attrCount + so.unifCount; i++)
    {
        const QueuedVar &q = s_vars[so.varFirst + i];
        ScriptDrawVar_t v;
        v.location = q.location;
        v.type = q.type;
        v.count = q.count;
        v.data = reinterpret_cast<const uint8_t *>(s_varData.data()) + q.offset;
        s_drawVars.push_back(v);
    }

    d.attributes = s_drawVars.data();
    d.attributeCount = (int)so.attrCount;
    d.uniforms = s_drawVars.data() + so.attrCount;
    d.uniformCount = (int)so.unifCount;

    g_render->scriptDraw(d);
}

void drawOp(const DrawOp &op, double camX, double camY)
{
    double offX = op.scene ? camX : 0.0;
    double offY = op.scene ? camY : 0.0;

    switch(op.kind)
    {
    case OP_TRIANGLES:
    {
        s_xy.resize(op.count * 2);
        s_uv.resize(op.count * 2);
        s_rgba.resize(op.count * 4);
        for(uint32_t i = 0; i < op.count; i++)
        {
            const DrawVertex &v = s_vertices[op.first + i];
            s_xy[2 * i] = (float)(v.x + offX);
            s_xy[2 * i + 1] = (float)(v.y + offY);
            s_uv[2 * i] = v.u;
            s_uv[2 * i + 1] = v.v;
            s_rgba[4 * i] = v.r;
            s_rgba[4 * i + 1] = v.g;
            s_rgba[4 * i + 2] = v.b;
            s_rgba[4 * i + 3] = v.a;
        }
        XRender::renderTriangles(op.picture, s_xy.data(), s_uv.data(), s_rgba.data(), (int)op.count,
                                 op.color, op.picture != nullptr);
        break;
    }

    case OP_IMAGE:
    {
        StdPicture &pic = *op.picture;
        int w = std::min((int)op.first, pic.w - op.u);
        int h = std::min((int)op.count, pic.h - op.v);
        if(w <= 0 || h <= 0)
            return;

        float x0 = (float)std::lround(op.x + offX), y0 = (float)std::lround(op.y + offY);
        float x1 = x0 + w, y1 = y0 + h;
        float u0 = (float)op.u / pic.w, v0 = (float)op.v / pic.h;
        float u1 = (float)(op.u + w) / pic.w, v1 = (float)(op.v + h) / pic.h;
        const float xy[12] = {x0, y0, x1, y0, x0, y1, x1, y0, x1, y1, x0, y1};
        const float uv[12] = {u0, v0, u1, v0, u0, v1, u1, v0, u1, v1, u0, v1};
        XRender::renderTriangles(op.picture, xy, uv, nullptr, 6, op.color, true);
        break;
    }

    case OP_TEXT:
        SuperPrint(s_texts[op.first].c_str(), (int)op.count,
                   (int)std::lround(op.x + offX), (int)std::lround(op.y + offY));
        break;

    case OP_SCRIPT:
        drawScriptOp(s_scriptOps[op.first], offX, offY);
        break;

    case OP_CAPTURE:
        g_render->scriptBufferCapture((int)op.first);
        break;

    case OP_CLEAR:
        g_render->scriptBufferClear((int)op.first);
        break;

    case OP_REDIRECT:
        s_redirects.emplace_back(op.count, (int)op.first);
        g_render->scriptRedirect((int)op.first);
        break;

    case OP_UNREDIRECT:
        if(!s_redirects.empty() && s_redirects.back().first == op.count)
        {
            // back to the redirect it interrupted, or the screen
            s_redirects.pop_back();
            g_render->scriptRedirect(s_redirects.empty() ? 0 : s_redirects.back().second);
        }
        else
        {
            // ending a redirect that a later one interrupted: just forget it
            for(auto it = s_redirects.begin(); it != s_redirects.end(); ++it)
            {
                if(it->first == op.count)
                {
                    s_redirects.erase(it);
                    break;
                }
            }
        }
        break;
    }
}

// LunaLua drops a camera's redirects when it moves on to the next camera
void endRedirects()
{
    if(s_redirects.empty())
        return;

    s_redirects.clear();
    g_render->scriptRedirect(0);
}

// Testing: saves the frame just drawn as smbx2shot-N.png in the log folder (the
// engine's own screenshots go to the Mac's screenshot folder)
void saveShot()
{
    const int w = XRender::TargetW, h = XRender::TargetH;
    std::vector<uint8_t> pixels((size_t)w * h * 4);
    XRender::setTargetTexture();
    g_render->getScreenPixelsRGBA(0, 0, w, h, pixels.data());
    XRender::setTargetScreen();

    FIBITMAP *img = FreeImage_AllocateT(FIT_BITMAP, w, h, 32);
    if(!img)
        return;

    // as AbstractRender_t::makeShot
    const uint8_t *px = pixels.data();
    RGBQUAD p;
    for(int y = 0; y < h; ++y)
    {
        for(int x = 0; x < w; ++x)
        {
            p.rgbRed = px[0];
            p.rgbGreen = px[1];
            p.rgbBlue = px[2];
            p.rgbReserved = px[3];
            FreeImage_SetPixelColor(img, x, (h - 1) - y, &p);
            px += 4;
        }
    }

    std::string path = AppPathManager::logsDir() + "smbx2shot-" + std::to_string(++s_shotCount) + ".png";
    if(FreeImage_Save(FIF_PNG, img, path.c_str(), 0))
        pLogInfo("smbx2compat: screenshot %s", path.c_str());
    else
        pLogWarning("smbx2compat: couldn't save %s", path.c_str());
    FreeImage_Unload(img);
}

void freePictures()
{
    s_images.clear();
    s_pictures.clear();
}


// Lua side (registered as globals, used by smbx2compat/core/lunadll.lua)

// xtech_gfx_imageLoad(path) -> id, w, h; nil if it can't be loaded
int lua_imageLoad(lua_State *L)
{
    std::string path = luaL_checkstring(L, 1);

    auto it = s_pictures.find(path);
    if(it == s_pictures.end())
    {
        std::unique_ptr<StdPicture> pic(new StdPicture());
        XRender::lazyLoadPicture(*pic, path);
        if(!pic->inited)
            return 0;
        pic->l.premultiply = true;
        it = s_pictures.emplace(path, std::move(pic)).first;
    }

    uintptr_t id = s_nextImage++;
    s_images[id] = it->second.get();
    lua_pushnumber(L, (lua_Number)id);
    lua_pushinteger(L, it->second->w);
    lua_pushinteger(L, it->second->h);
    return 3;
}

int lua_imageFree(lua_State *L)
{
    s_images.erase((uintptr_t)luaL_checknumber(L, 1));
    return 0;
}

// xtech_gfx_setSpriteOverride(category, index, path | nil) -> true if the
// engine has that sprite (SpriteOverride_Set); nil restores it
int lua_setSpriteOverride(lua_State *L)
{
    std::string category = luaL_checkstring(L, 1);
    int index = (int)luaL_checkinteger(L, 2);
    const char *path = lua_isnoneornil(L, 3) ? "" : luaL_checkstring(L, 3);
    lua_pushboolean(L, SpriteOverride_Set(category, index, path));
    return 1;
}

// xtech_gfx_setVanilla(what, draw): what = XTechLuaVanillaDraw
int lua_setVanilla(lua_State *L)
{
    int what = (int)luaL_checkinteger(L, 1);
    bool draw = lua_toboolean(L, 2) != 0;
    if(what < 0 || what >= XTECH_LUA_DRAW_COUNT)
        return 0;

    s_vanilla[what] = draw;
    if(what == XTECH_LUA_DRAW_HUD)
    {
        // TheXTech's Luna autocode switch for the same thing
        gSMBXHUDSettings.skip = !draw;
        s_hudSkipped = !draw;
    }
    return 0;
}

int lua_getVanilla(lua_State *L)
{
    int what = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, what >= 0 && what < XTECH_LUA_DRAW_COUNT && s_vanilla[what]);
    return 1;
}

// xtech_gfx_setFramebufferSize(w, h): LunaLua's FFI_GraphicsSetMainFramebufferSize
// (SMATRS plays at 960x540). Set as a script's setting, which lasts until the next
// level rebuilds the config.
int lua_setFramebufferSize(lua_State *L)
{
    int w = (int)luaL_checkinteger(L, 1);
    int h = (int)luaL_checkinteger(L, 2);
    if(w <= 0 || h <= 0)
        return 0;

    ConfigChangeSentinel sent(ConfigSetLevel::script);
    g_config.internal_res = {w, h};
    s_fbW = w;
    s_fbH = h;
    return 0;
}

// The size asked for, before the engine applies it (scripts make capture buffers that size)
int lua_getFramebufferSize(lua_State *L)
{
    lua_pushinteger(L, s_fbW ? s_fbW : XRender::TargetW);
    lua_pushinteger(L, s_fbH ? s_fbH : XRender::TargetH);
    return 2;
}

// Testing (SMBX2COMPAT_SHOTS in boot.lua): a screenshot of the frame being drawn
int lua_screenshot(lua_State *)
{
    s_shotDue = true;
    return 0;
}

// xtech_gfx_shaderHeader() -> the "#version" line(s) glsl.lua translates for, or nil
int lua_shaderHeader(lua_State *L)
{
    const char *header = g_render->scriptShaderHeader();
    if(!header)
        return 0;
    lua_pushstring(L, header);
    return 1;
}

// xtech_gfx_shaderCompile(handle, vertexSource, fragmentSource) -> true | false, error
int lua_shaderCompile(lua_State *L)
{
    uintptr_t key = (uintptr_t)luaL_checknumber(L, 1);
    const char *vs = luaL_checkstring(L, 2);
    const char *fs = luaL_checkstring(L, 3);

    std::string error;
    int id = g_render->scriptShaderCreate(vs, fs, error);
    if(!id)
    {
        lua_pushboolean(L, 0);
        lua_pushstring(L, error.c_str());
        return 2;
    }

    auto it = s_shaders.find(key);
    if(it != s_shaders.end())
        s_freedShaders.push_back(it->second);
    s_shaders[key] = id;
    lua_pushboolean(L, 1);
    return 1;
}

// xtech_gfx_shaderVariables(handle, attributes) -> {{name=, location=, type=, size=}, ...}
int lua_shaderVariables(lua_State *L)
{
    uintptr_t key = (uintptr_t)luaL_checknumber(L, 1);
    bool attributes = lua_toboolean(L, 2) != 0;

    auto it = s_shaders.find(key);
    if(it == s_shaders.end())
        return 0;

    std::vector<ScriptShaderVar_t> vars;
    g_render->scriptShaderVariables(it->second, attributes, vars);

    lua_createtable(L, (int)vars.size(), 0);
    for(size_t i = 0; i < vars.size(); i++)
    {
        lua_createtable(L, 0, 4);
        lua_pushstring(L, vars[i].name.c_str());
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, vars[i].location);
        lua_setfield(L, -2, "location");
        lua_pushnumber(L, (lua_Number)vars[i].type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, vars[i].size);
        lua_setfield(L, -2, "size");
        lua_rawseti(L, -2, (int)i + 1);
    }
    return 1;
}

int lua_shaderFree(lua_State *L)
{
    auto it = s_shaders.find((uintptr_t)luaL_checknumber(L, 1));
    if(it != s_shaders.end())
    {
        s_freedShaders.push_back(it->second);
        s_shaders.erase(it);
    }
    return 0;
}

// xtech_gfx_setStandardShader(handle): draws without a shader use it
int lua_setStandardShader(lua_State *L)
{
    s_standardShader = shaderFor(reinterpret_cast<void *>((uintptr_t)luaL_checknumber(L, 1)));
    return 0;
}

// xtech_gfx_bufferCreate(handle, w, h) -> true if the renderer made it
int lua_bufferCreate(lua_State *L)
{
    uintptr_t key = (uintptr_t)luaL_checknumber(L, 1);
    int w = (int)luaL_checkinteger(L, 2);
    int h = (int)luaL_checkinteger(L, 3);

    int id = g_render->scriptBufferCreate(w, h);
    if(id)
        s_buffers[key] = id;
    lua_pushboolean(L, id != 0);
    return 1;
}

int lua_bufferFree(lua_State *L)
{
    auto it = s_buffers.find((uintptr_t)luaL_checknumber(L, 1));
    if(it != s_buffers.end())
    {
        s_freedBuffers.push_back(it->second);
        s_buffers.erase(it);
    }
    return 0;
}

// xtech_gfx_bufferCapture(handle, priority) / xtech_gfx_bufferClear(handle, priority)
int queueBufferOp(lua_State *L, OpKind kind)
{
    int id = bufferFor(reinterpret_cast<void *>((uintptr_t)luaL_checknumber(L, 1)));
    double priority = luaL_checknumber(L, 2);
    if(!id)
        return 0;

    makeRoom();
    DrawOp &op = newOp(kind, priority, false);
    op.first = (uint32_t)id;
    return 0;
}

int lua_bufferCapture(lua_State *L)
{
    return queueBufferOp(L, OP_CAPTURE);
}

int lua_bufferClear(lua_State *L)
{
    return queueBufferOp(L, OP_CLEAR);
}

// xtech_gfx_redirect(handle, startPriority, endPriority): the engine's and the
// screen's draws from startPriority to endPriority go into the capture buffer
int lua_redirect(lua_State *L)
{
    int id = bufferFor(reinterpret_cast<void *>((uintptr_t)luaL_checknumber(L, 1)));
    double startPriority = luaL_checknumber(L, 2);
    double endPriority = luaL_checknumber(L, 3);
    if(!id)
        return 0;

    makeRoom();
    uint32_t handle = ++s_nextRedirect;
    DrawOp &start = newOp(OP_REDIRECT, startPriority, false);
    start.first = (uint32_t)id;
    start.count = handle;
    DrawOp &end = newOp(OP_UNREDIRECT, endPriority, false);
    end.count = handle;
    return 0;
}

// Frees the pictures when the level's Lua state closes
int lua_ownerGC(lua_State *)
{
    xtech_lua_drawReset();
    return 0;
}

} // namespace


bool xtech_lua_drawVanilla(XTechLuaVanillaDraw what)
{
    return s_vanilla[what];
}

void xtech_lua_drawCameraStart(int screenZ)
{
    s_curCamera = screenZ;
    s_drawScreen = screenZ;
    s_pass++;
    s_orderCursor = 0;
}

void xtech_lua_cameraEnd()
{
    endRedirects();
    s_drawScreen = 0;
}

void xtech_lua_drawBelow(double maxPriority)
{
    if(s_drawScreen == 0 || s_ops.empty())
        return;

    // sort draws queued since the last pass (a stable order keeps queue order on ties)
    if(s_order.size() != s_ops.size())
    {
        s_order.resize(s_ops.size());
        for(uint32_t i = 0; i < (uint32_t)s_order.size(); i++)
            s_order[i] = i;
        std::stable_sort(s_order.begin(), s_order.end(),
            [](uint32_t a, uint32_t b) { return s_ops[a].priority < s_ops[b].priority; });
        s_orderCursor = 0;
    }

    const vScreen_t &vs = vScreen[s_drawScreen];
    double camX = (double)vs.CameraAddX();
    double camY = (double)vs.CameraAddY();

    while(s_orderCursor < s_order.size() && s_ops[s_order[s_orderCursor]].drawnPass == s_pass)
        s_orderCursor++;

    bool scriptDrawn = false;
    for(size_t i = s_orderCursor; i < s_order.size(); i++)
    {
        DrawOp &op = s_ops[s_order[i]];
        if(op.priority >= maxPriority)
            break;
        if(op.drawnPass == s_pass)
            continue;

        op.drawnPass = s_pass;
        if(op.camera == 0 || op.camera == s_drawScreen)
        {
            drawOp(op, camX, camY);
            scriptDrawn |= op.kind >= OP_SCRIPT;
        }
    }

    // the engine draws next
    if(scriptDrawn)
        g_render->scriptDrawEnd();
}

void xtech_lua_drawFrameEnd()
{
    if(s_shotDue)
    {
        s_shotDue = false;
        saveShot();
    }

    s_ops.clear();
    s_vertices.clear();
    s_texts.clear();
    s_scriptOps.clear();
    s_floats.clear();
    s_vars.clear();
    s_varData.clear();
    s_order.clear();
    s_orderCursor = 0;
    s_curCamera = 0;
    s_drawScreen = 0;
    endRedirects();

    for(int id : s_freedShaders)
        g_render->scriptShaderFree(id);
    s_freedShaders.clear();
    for(int id : s_freedBuffers)
        g_render->scriptBufferFree(id);
    s_freedBuffers.clear();
}

void xtech_lua_drawReset()
{
    for(auto &it : s_shaders)
        s_freedShaders.push_back(it.second);
    s_shaders.clear();
    for(auto &it : s_buffers)
        s_freedBuffers.push_back(it.second);
    s_buffers.clear();
    s_standardShader = 0;
    s_fbW = s_fbH = 0;

    xtech_lua_drawFrameEnd();
    freePictures();
    SpriteOverride_Reset();
    for(bool &v : s_vanilla)
        v = true;
    if(s_hudSkipped)
    {
        gSMBXHUDSettings.skip = false;
        s_hudSkipped = false;
    }
}

void xtech_lua_register_draw(lua_State *L)
{
    lua_register(L, "xtech_gfx_imageLoad", lua_imageLoad);
    lua_register(L, "xtech_gfx_imageFree", lua_imageFree);
    lua_register(L, "xtech_gfx_setSpriteOverride", lua_setSpriteOverride);
    lua_register(L, "xtech_gfx_setVanilla", lua_setVanilla);
    lua_register(L, "xtech_gfx_getVanilla", lua_getVanilla);
    lua_register(L, "xtech_gfx_screenshot", lua_screenshot);
    lua_register(L, "xtech_gfx_setFramebufferSize", lua_setFramebufferSize);
    lua_register(L, "xtech_gfx_getFramebufferSize", lua_getFramebufferSize);
    lua_register(L, "xtech_gfx_shaderHeader", lua_shaderHeader);
    lua_register(L, "xtech_gfx_shaderCompile", lua_shaderCompile);
    lua_register(L, "xtech_gfx_shaderVariables", lua_shaderVariables);
    lua_register(L, "xtech_gfx_shaderFree", lua_shaderFree);
    lua_register(L, "xtech_gfx_setStandardShader", lua_setStandardShader);
    lua_register(L, "xtech_gfx_bufferCreate", lua_bufferCreate);
    lua_register(L, "xtech_gfx_bufferFree", lua_bufferFree);
    lua_register(L, "xtech_gfx_bufferCapture", lua_bufferCapture);
    lua_register(L, "xtech_gfx_bufferClear", lua_bufferClear);
    lua_register(L, "xtech_gfx_redirect", lua_redirect);

    lua_pushlightuserdata(L, reinterpret_cast<void *>(&ffi_glDraw));
    lua_setglobal(L, "__xtech_ffiGLDraw");
    lua_pushlightuserdata(L, reinterpret_cast<void *>(&ffi_imageDraw));
    lua_setglobal(L, "__xtech_ffiImageDraw");
    lua_pushlightuserdata(L, reinterpret_cast<void *>(&ffi_textDraw));
    lua_setglobal(L, "__xtech_ffiTextDraw");
    lua_pushlightuserdata(L, reinterpret_cast<void *>(&ffi_alloc));
    lua_setglobal(L, "__xtech_ffiAlloc");

    // pictures belong to this Lua state; free them with it
    lua_newuserdata(L, 1);
    lua_newtable(L);
    lua_pushcfunction(L, lua_ownerGC);
    lua_setfield(L, -2, "__gc");
    lua_setmetatable(L, -2);
    lua_setfield(L, LUA_REGISTRYINDEX, "xtech_gfx.owner");
}
