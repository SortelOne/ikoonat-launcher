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
// shared by everything that loaded the same file.

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

// Pictures by file, kept until the level's Lua state closes
std::unordered_map<std::string, std::unique_ptr<StdPicture>> s_pictures;
// LunaImageRef numbers -> pictures
std::unordered_map<uintptr_t, StdPicture *> s_images;
uintptr_t s_nextImage = 1;

bool s_vanilla[XTECH_LUA_DRAW_COUNT] = {true, true, true, true};
bool s_hudSkipped = false;

enum OpKind : uint8_t
{
    OP_TRIANGLES,
    OP_IMAGE,
    OP_TEXT,
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

// drawing order: indices into s_ops sorted by priority (queue order on ties)
std::vector<uint32_t> s_order;
size_t s_orderCursor = 0;

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
    if(s_ops.size() >= c_maxQueuedOps || s_vertices.size() >= c_maxQueuedVertices)
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

void pushVertex(const FFI_GL_Draw_Cmd &cmd, uint32_t i)
{
    DrawVertex v;
    v.x = cmd.mVert[2 * i];
    v.y = cmd.mVert[2 * i + 1];
    v.u = cmd.mTex ? cmd.mTex[2 * i] : 0.0f;
    v.v = cmd.mTex ? cmd.mTex[2 * i + 1] : 0.0f;

    if(cmd.mVertColor)
    {
        // SMBX2's vertex colours are premultiplied; the engine blends straight alpha
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

void ffi_glDraw(const FFI_GL_Draw_Cmd *cmd)
{
    static bool warnedTarget = false, warnedCapture = false, warnedShader = false,
                warnedClip = false, warnedPrimitive = false;

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

    if(cmd->mTarget)
    {
        warnOnce(warnedTarget, "Graphics.glDraw into a capture buffer isn't supported yet (skipped)");
        return;
    }

    if(cmd->mCap)
    {
        warnOnce(warnedCapture, "Graphics.glDraw from a capture buffer isn't supported yet (skipped)");
        return;
    }

    if(cmd->mShader)
        warnOnce(warnedShader, "Graphics.glDraw shaders aren't supported yet (drawn without)");

    if(cmd->mNumClipPlane)
        warnOnce(warnedClip, "Graphics.glDraw clip planes aren't supported yet (ignored)");

    if(!cmd->mVert || cmd->mCount < 3)
        return;

    makeRoom();
    uint32_t first = (uint32_t)s_vertices.size();
    uint32_t n = cmd->mCount;

    switch(cmd->mType)
    {
    case GL_PRIM_TRIANGLES:
        for(uint32_t i = 0; i + 2 < n; i += 3)
        {
            pushVertex(*cmd, i);
            pushVertex(*cmd, i + 1);
            pushVertex(*cmd, i + 2);
        }
        break;

    case GL_PRIM_TRIANGLE_STRIP:
        for(uint32_t i = 0; i + 2 < n; i++)
        {
            // keep the winding of every other triangle
            pushVertex(*cmd, (i & 1) ? i + 1 : i);
            pushVertex(*cmd, (i & 1) ? i : i + 1);
            pushVertex(*cmd, i + 2);
        }
        break;

    case GL_PRIM_TRIANGLE_FAN:
        for(uint32_t i = 1; i + 1 < n; i++)
        {
            pushVertex(*cmd, 0);
            pushVertex(*cmd, i);
            pushVertex(*cmd, i + 1);
        }
        break;

    default:
        warnOnce(warnedPrimitive, "Graphics.glDraw points and lines aren't supported yet (skipped)");
        return;
    }

    DrawOp &op = newOp(OP_TRIANGLES, cmd->mPriority, cmd->mSceneCoords);
    op.picture = pictureFor(cmd->mImg);
    op.color = XTColor(toByte(cmd->mColor[0]), toByte(cmd->mColor[1]),
                       toByte(cmd->mColor[2]), toByte(cmd->mColor[3]));
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
    op.color = XTColor(255, 255, 255, toByte(opacity));
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
        XRender::renderTriangles(op.picture, s_xy.data(), s_uv.data(), s_rgba.data(), (int)op.count, op.color);
        break;
    }

    case OP_IMAGE:
    {
        StdPicture &pic = *op.picture;
        int w = std::min((int)op.first, pic.w - op.u);
        int h = std::min((int)op.count, pic.h - op.v);
        if(w <= 0 || h <= 0)
            return;
        XRender::renderTextureBasic((int)std::lround(op.x + offX), (int)std::lround(op.y + offY),
                                    w, h, pic, op.u, op.v, op.color);
        break;
    }

    case OP_TEXT:
        SuperPrint(s_texts[op.first].c_str(), (int)op.count,
                   (int)std::lround(op.x + offX), (int)std::lround(op.y + offY));
        break;
    }
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
    return 0;
}

int lua_getFramebufferSize(lua_State *L)
{
    lua_pushinteger(L, XRender::TargetW);
    lua_pushinteger(L, XRender::TargetH);
    return 2;
}

// Testing (SMBX2COMPAT_SHOTS in boot.lua): a screenshot of the frame being drawn
int lua_screenshot(lua_State *)
{
    s_shotDue = true;
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

    for(size_t i = s_orderCursor; i < s_order.size(); i++)
    {
        DrawOp &op = s_ops[s_order[i]];
        if(op.priority >= maxPriority)
            break;
        if(op.drawnPass == s_pass)
            continue;

        op.drawnPass = s_pass;
        if(op.camera == 0 || op.camera == s_drawScreen)
            drawOp(op, camX, camY);
    }
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
    s_order.clear();
    s_orderCursor = 0;
    s_curCamera = 0;
    s_drawScreen = 0;
}

void xtech_lua_drawReset()
{
    xtech_lua_drawFrameEnd();
    freePictures();
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
    lua_register(L, "xtech_gfx_setVanilla", lua_setVanilla);
    lua_register(L, "xtech_gfx_getVanilla", lua_getVanilla);
    lua_register(L, "xtech_gfx_screenshot", lua_screenshot);
    lua_register(L, "xtech_gfx_setFramebufferSize", lua_setFramebufferSize);
    lua_register(L, "xtech_gfx_getFramebufferSize", lua_getFramebufferSize);

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
