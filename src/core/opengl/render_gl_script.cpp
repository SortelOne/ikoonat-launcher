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

// smbx2compat: SMBX2's (LunaLua's) shader programs and capture buffers.
//
// These draws work like LunaLua's GLEngineCmd_LuaDraw: each is its own GL draw
// call with its own program, uniforms and textures, blended premultiplied
// (GL_ONE, GL_ONE_MINUS_SRC_ALPHA). The engine's queued draws are drawn first
// (flushDrawQueues), so a script draw lands above everything queued before it.
// It neither tests nor writes depth, so later opaque draws still cover it,
// unless it asks for a depth test (lib3d's 3D meshes): then, in a capture
// buffer, it tests and writes that buffer's own depth as LunaLua does
// (GL_LEQUAL, cleared to the far plane with the buffer).
//
// Sources come translated to this context's GLSL by smbx2compat/core/glsl.lua,
// which turns SMBX2's GLSL 1.20 built-ins into these names:
//   xt_Vertex, xt_MultiTexCoord0, xt_Color   attributes 0, 1, 2
//   xt_MVP, xt_MV, xt_P, xt_TexMatrix0       uniforms set here
// Capture buffers keep rows top-down like the game buffer (row 0 = y 0), as
// LunaLua's framebuffers do, so gl_FragCoord and texture coordinates match.
//
// A capture buffer stays bound after a draw into it until something needs the
// engine's framebuffer again (scriptDrawEnd at the latest): each switch starts
// a new render pass on tiled GPUs, which Apple's OpenGL makes costly.
//
// Graphics.redirectCameraFB (scriptRedirect) makes a capture buffer the
// engine's framebuffer for a while: it takes BUFFER_GAME's slot, so the
// engine's own draws, its passes that rebind the game buffer (framebuffer
// copies, lighting, multipass) and script draws to the screen all land in it,
// with the camera's view at its top-left as in LunaLua's camera framebuffers.
// It gets a depth buffer of its own, cleared at each redirect, for the
// engine's opaque draws. The engine's copies use game-buffer coordinates, which
// match only for an unscaled camera at the screen's top-left (one player).

#include <array>
#include <cstring>

#include <Logger/logger.h>

#include "core/opengl/gl_inc.h"
#include "core/opengl/render_gl.h"
#include "core/opengl/gl_program_object.h"
#include "core/render.h"

#if defined(RENDERGL_HAS_SHADERS) && defined(RENDERGL_HAS_FBO)

namespace
{

using Mat4 = std::array<GLfloat, 16>;

// column-major, as GL takes them
Mat4 mat4Multiply(const Mat4 &a, const Mat4 &b)
{
    Mat4 r;
    for(int c = 0; c < 4; c++)
    {
        for(int row = 0; row < 4; row++)
        {
            GLfloat sum = 0.0f;
            for(int k = 0; k < 4; k++)
                sum += a[k * 4 + row] * b[c * 4 + k];
            r[c * 4 + row] = sum;
        }
    }
    return r;
}

Mat4 mat4Scale(GLfloat x, GLfloat y)
{
    return {x, 0.0f, 0.0f, 0.0f,
            0.0f, y, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f};
}

Mat4 mat4Translate(GLfloat x, GLfloat y)
{
    return {1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            x, y, 0.0f, 1.0f};
}

void warnOnce(bool &warned, const char *what, unsigned type)
{
    if(!warned)
    {
        warned = true;
        pLogWarning("smbx2compat: %s (GL type 0x%04X)", what, type);
    }
}

GLuint compileShader(GLenum type, const char *src, std::string &error)
{
    GLuint shader = glCreateShader(type);
    if(!shader)
    {
        error = "couldn't create a shader object";
        return 0;
    }

    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if(status)
        return shader;

    // LunaLua's wording (GLShader::load)
    error = (type == GL_VERTEX_SHADER) ? "Vertex Shader Error: \n" : "Fragment Shader Error: \n";

    GLint len = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
    if(len > 1)
    {
        std::string log((size_t)len, '\0');
        glGetShaderInfoLog(shader, len, nullptr, &log[0]);
        error += log.c_str();
    }

    glDeleteShader(shader);
    return 0;
}

// Components per element of a float uniform/attribute type; 0 if not a float type
int floatComponents(unsigned type)
{
    switch(type)
    {
    case GL_FLOAT:      return 1;
    case GL_FLOAT_VEC2: return 2;
    case GL_FLOAT_VEC3: return 3;
    case GL_FLOAT_VEC4: return 4;
    default:            return 0;
    }
}

} // namespace


GLuint RenderGL::scriptCurrentFB() const
{
    return (m_recentTargetScreen || !m_game_texture_fb) ? 0 : m_buffer_fb[m_cur_pass_target];
}

void RenderGL::scriptBindBuffer(int id, const ScriptBuffer_t &b)
{
    if(m_script_bound_buffer == id)
        return;

    // drawing to the screen rather than the game buffer: only GL knows the viewport
    if(!m_script_bound_buffer && !scriptCurrentFB())
        glGetIntegerv(GL_VIEWPORT, m_script_saved_viewport);

    glBindFramebuffer(GL_FRAMEBUFFER, b.fb);
    glViewport(0, 0, b.w, b.h);
    m_script_bound_buffer = id;
}

bool RenderGL::scriptAddDepth(ScriptBuffer_t &b)
{
    if(b.depth_rb)
        return true;

    // 24 bits, as LunaLua's depth textures: with 16, lib3d's nearly flat
    // objects (SMATRS's storybook pages and envelopes) fight through each other.
    // OpenGL ES 2 may only have 16.
    glGenRenderbuffers(1, &b.depth_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, b.depth_rb);
    while(glGetError() != GL_NO_ERROR) {}
    glRenderbufferStorage(GL_RENDERBUFFER, 0x81A6 /* GL_DEPTH_COMPONENT24 */, b.w, b.h);
    if(glGetError() != GL_NO_ERROR)
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, b.w, b.h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, b.depth_rb);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        pLogWarning("smbx2compat: couldn't add a depth buffer to a %dx%d capture buffer", b.w, b.h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
        glDeleteRenderbuffers(1, &b.depth_rb);
        b.depth_rb = 0;
        return false;
    }

    return true;
}

// clears the bound framebuffer's depth to value, then puts back the engine's
// clear value (render_gl_init.cpp)
void RenderGL::scriptClearDepth(GLfloat value)
{
#if (defined(THEXTECH_BUILD_GL_DESKTOP_MODERN) || defined(THEXTECH_BUILD_GL_DESKTOP_LEGACY)) && (defined(THEXTECH_BUILD_GL_ES_MODERN) || defined(THEXTECH_BUILD_GL_ES_LEGACY))
    if(m_gl_profile != SDL_GL_CONTEXT_PROFILE_ES)
        glClearDepth(value);
    else
        glClearDepthf(value);
    glClear(GL_DEPTH_BUFFER_BIT);
    if(m_gl_profile != SDL_GL_CONTEXT_PROFILE_ES)
        glClearDepth(-1.0f);
    else
        glClearDepthf(-1.0f);
#elif defined(THEXTECH_BUILD_GL_ES_MODERN) || defined(THEXTECH_BUILD_GL_ES_LEGACY)
    glClearDepthf(value);
    glClear(GL_DEPTH_BUFFER_BIT);
    glClearDepthf(-1.0f);
#else
    glClearDepth(value);
    glClear(GL_DEPTH_BUFFER_BIT);
    glClearDepth(-1.0f);
#endif
}

void RenderGL::scriptDrawEnd()
{
    if(!m_script_bound_buffer)
        return;

    m_script_bound_buffer = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, scriptCurrentFB());

    if(scriptCurrentFB())
        applyViewport();
    else
        glViewport(m_script_saved_viewport[0], m_script_saved_viewport[1], m_script_saved_viewport[2], m_script_saved_viewport[3]);
}

void RenderGL::scriptRedirect(int buffer)
{
    ScriptBuffer_t *b = nullptr;
    if(buffer)
    {
        auto it = m_script_buffers.find(buffer);
        if(it == m_script_buffers.end())
            buffer = 0;
        else
            b = &it->second;
    }

    // only while drawing to the game buffer (a level's cameras)
    if(m_recentTargetScreen || !m_game_texture_fb)
    {
        buffer = 0;
        b = nullptr;
    }

    if(buffer == m_script_redirect)
        return;

    // what's queued so far goes where it was drawn
    scriptDrawEnd();
    flushDrawQueues();

    // the capture buffer stands in for the game buffer, so every engine pass
    // (framebuffer copies, lighting, multipass) draws into it too
    if(!m_script_redirect)
    {
        m_script_game_fb = m_buffer_fb[BUFFER_GAME];
        m_script_game_texture = m_buffer_texture[BUFFER_GAME];
    }

    m_script_redirect = buffer;
    m_buffer_fb[BUFFER_GAME] = b ? b->fb : m_script_game_fb;
    m_buffer_texture[BUFFER_GAME] = b ? b->texture : m_script_game_texture;

    if(m_recentTargetScreen)
        return;

    if(b && m_use_depth_buffer && !b->depth_rb)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, b->fb);
        scriptAddDepth(*b);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, scriptCurrentFB());

    // the engine's depth values only order its draws within this redirect
    if(b && m_use_depth_buffer)
        glClear(GL_DEPTH_BUFFER_BIT);

    applyViewport();
}

const char *RenderGL::scriptShaderHeader()
{
    if(!m_use_shaders || !m_has_fbo || !m_game_texture_fb)
        return nullptr;

    if(m_gl_profile == SDL_GL_CONTEXT_PROFILE_ES)
        return m_has_es3_shaders ? "#version 300 es\n" : "#version 100\n";

    if(m_gl_majver > 3 || (m_gl_majver == 3 && m_gl_minver >= 3))
        return "#version 330\n";

    return nullptr;
}

// A shader that doesn't read one of the built-in attributes (lib3d's read no
// gl_Vertex) leaves its location free, and the linker may give it to one of the
// script's own attributes, which draws never fill (location 0 gets the vertex
// coordinates). So the script's attributes are bound after ours and the program
// relinked; true if that's needed.
static bool scriptMoveAttributes(GLuint program)
{
    GLint count = 0, max_len = 0;
    glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &count);
    glGetProgramiv(program, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &max_len);
    if(count <= 0 || max_len <= 0)
        return false;

    std::vector<std::string> names;
    bool clash = false;
    std::vector<char> name((size_t)max_len + 1, '\0');
    for(GLint i = 0; i < count; i++)
    {
        GLsizei len = 0;
        GLint size = 0;
        GLenum type = 0;
        glGetActiveAttrib(program, (GLuint)i, (GLsizei)name.size(), &len, &size, &type, name.data());
        std::string n(name.data(), (size_t)len);
        if(n.compare(0, 3, "xt_") == 0 || n.compare(0, 3, "gl_") == 0)
            continue;

        names.push_back(n);
        if(glGetAttribLocation(program, n.c_str()) <= 2)
            clash = true;
    }

    if(!clash)
        return false;

    for(size_t i = 0; i < names.size(); i++)
        glBindAttribLocation(program, (GLuint)(3 + i), names[i].c_str());

    return true;
}

// Values per element of a uniform type
static int uniformComponents(GLenum type)
{
    switch(type)
    {
    case GL_FLOAT: case GL_INT: case GL_BOOL:          return 1;
    case GL_FLOAT_VEC2: case GL_INT_VEC2: case GL_BOOL_VEC2: return 2;
    case GL_FLOAT_VEC3: case GL_INT_VEC3: case GL_BOOL_VEC3: return 3;
    case GL_FLOAT_VEC4: case GL_INT_VEC4: case GL_BOOL_VEC4: return 4;
    case GL_FLOAT_MAT2: return 4;
    case GL_FLOAT_MAT3: return 9;
    case GL_FLOAT_MAT4: return 16;
#ifdef THEXTECH_BUILD_GL_DESKTOP_MODERN
    case GL_UNSIGNED_INT:      return 1;
    case GL_UNSIGNED_INT_VEC2: return 2;
    case GL_UNSIGNED_INT_VEC3: return 3;
    case GL_UNSIGNED_INT_VEC4: return 4;
    case GL_FLOAT_MAT2x3: case GL_FLOAT_MAT3x2: return 6;
    case GL_FLOAT_MAT2x4: case GL_FLOAT_MAT4x2: return 8;
    case GL_FLOAT_MAT3x4: case GL_FLOAT_MAT4x3: return 12;
#endif
    default: return 1; // samplers
    }
}

static bool uniformIsFloat(GLenum type)
{
    switch(type)
    {
    case GL_FLOAT: case GL_FLOAT_VEC2: case GL_FLOAT_VEC3: case GL_FLOAT_VEC4:
    case GL_FLOAT_MAT2: case GL_FLOAT_MAT3: case GL_FLOAT_MAT4:
#ifdef THEXTECH_BUILD_GL_DESKTOP_MODERN
    case GL_FLOAT_MAT2x3: case GL_FLOAT_MAT2x4: case GL_FLOAT_MAT3x2:
    case GL_FLOAT_MAT3x4: case GL_FLOAT_MAT4x2: case GL_FLOAT_MAT4x3:
#endif
        return true;
    default:
        return false;
    }
}

// Sets one element of a uniform (the program in use) from raw values
static void setUniformElement(GLint loc, GLenum type, const GLuint *raw)
{
    const GLfloat *f = reinterpret_cast<const GLfloat *>(raw);
    const GLint *iv = reinterpret_cast<const GLint *>(raw);

    switch(type)
    {
    case GL_FLOAT:        glUniform1fv(loc, 1, f); break;
    case GL_FLOAT_VEC2:   glUniform2fv(loc, 1, f); break;
    case GL_FLOAT_VEC3:   glUniform3fv(loc, 1, f); break;
    case GL_FLOAT_VEC4:   glUniform4fv(loc, 1, f); break;
    case GL_FLOAT_MAT2:   glUniformMatrix2fv(loc, 1, GL_FALSE, f); break;
    case GL_FLOAT_MAT3:   glUniformMatrix3fv(loc, 1, GL_FALSE, f); break;
    case GL_FLOAT_MAT4:   glUniformMatrix4fv(loc, 1, GL_FALSE, f); break;
    case GL_INT_VEC2:
    case GL_BOOL_VEC2:    glUniform2iv(loc, 1, iv); break;
    case GL_INT_VEC3:
    case GL_BOOL_VEC3:    glUniform3iv(loc, 1, iv); break;
    case GL_INT_VEC4:
    case GL_BOOL_VEC4:    glUniform4iv(loc, 1, iv); break;
#ifdef THEXTECH_BUILD_GL_DESKTOP_MODERN
    case GL_UNSIGNED_INT:      glUniform1uiv(loc, 1, raw); break;
    case GL_UNSIGNED_INT_VEC2: glUniform2uiv(loc, 1, raw); break;
    case GL_UNSIGNED_INT_VEC3: glUniform3uiv(loc, 1, raw); break;
    case GL_UNSIGNED_INT_VEC4: glUniform4uiv(loc, 1, raw); break;
    case GL_FLOAT_MAT2x3: glUniformMatrix2x3fv(loc, 1, GL_FALSE, f); break;
    case GL_FLOAT_MAT2x4: glUniformMatrix2x4fv(loc, 1, GL_FALSE, f); break;
    case GL_FLOAT_MAT3x2: glUniformMatrix3x2fv(loc, 1, GL_FALSE, f); break;
    case GL_FLOAT_MAT3x4: glUniformMatrix3x4fv(loc, 1, GL_FALSE, f); break;
    case GL_FLOAT_MAT4x2: glUniformMatrix4x2fv(loc, 1, GL_FALSE, f); break;
    case GL_FLOAT_MAT4x3: glUniformMatrix4x3fv(loc, 1, GL_FALSE, f); break;
#endif
    default: // int, bool and the samplers
        glUniform1iv(loc, 1, iv);
        break;
    }
}

// Keeps each uniform element's value as linked, for scriptShaderReset
// (initial: ScriptProgram_t::initial)
template<class InitialList>
static void readInitialUniforms(GLuint program, InitialList &initial, std::vector<GLuint> &values)
{
    GLint count = 0, max_len = 0;
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);
    glGetProgramiv(program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &max_len);

    std::vector<char> name((size_t)max_len + 1);
    for(GLint i = 0; i < count; i++)
    {
        GLsizei len = 0;
        GLint size = 0;
        GLenum type = 0;
        glGetActiveUniform(program, (GLuint)i, (GLsizei)name.size(), &len, &size, &type, name.data());

        // array elements one by one ("name[0]" is reported for arrays)
        std::string base(name.data(), (size_t)len);
        if(base.size() > 3 && base.compare(base.size() - 3, 3, "[0]") == 0)
            base.resize(base.size() - 3);

        int comps = uniformComponents(type);
        for(GLint k = 0; k < size; k++)
        {
            std::string el = size > 1 ? base + "[" + std::to_string(k) + "]" : base;
            GLint loc = glGetUniformLocation(program, el.c_str());
            if(loc < 0)
                continue; // in a uniform block

            size_t offset = values.size();
            values.resize(offset + 16, 0);
            if(uniformIsFloat(type))
                glGetUniformfv(program, loc, reinterpret_cast<GLfloat *>(&values[offset]));
            else
                glGetUniformiv(program, loc, reinterpret_cast<GLint *>(&values[offset]));
            values.resize(offset + (size_t)comps);

            initial.push_back({loc, type, offset});
        }
    }
}

int RenderGL::scriptShaderCreate(const char *vertex, const char *fragment, std::string &error)
{
    if(!scriptShaderHeader())
    {
        error = "shaders need OpenGL 3.3 or OpenGL ES";
        return 0;
    }

    GLuint vs = compileShader(GL_VERTEX_SHADER, vertex, error);
    if(!vs)
        return 0;

    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragment, error);
    if(!fs)
    {
        glDeleteShader(vs);
        return 0;
    }

    GLuint program = glCreateProgram();
    if(!program)
    {
        glDeleteShader(vs);
        glDeleteShader(fs);
        error = "couldn't create a program object";
        return 0;
    }

    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "xt_Vertex");
    glBindAttribLocation(program, 1, "xt_MultiTexCoord0");
    glBindAttribLocation(program, 2, "xt_Color");
    glLinkProgram(program);

    GLint status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if(status && scriptMoveAttributes(program))
    {
        glLinkProgram(program);
        glGetProgramiv(program, GL_LINK_STATUS, &status);
    }

    glDetachShader(program, vs);
    glDetachShader(program, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    if(!status)
    {
        error = "Shader Link Error: \n";

        GLint len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
        if(len > 1)
        {
            std::string log((size_t)len, '\0');
            glGetProgramInfoLog(program, len, nullptr, &log[0]);
            error += log.c_str();
        }

        glDeleteProgram(program);
        return 0;
    }

    ScriptProgram_t p;
    p.program = program;
    p.u_mvp = glGetUniformLocation(program, "xt_MVP");
    p.u_mv = glGetUniformLocation(program, "xt_MV");
    p.u_p = glGetUniformLocation(program, "xt_P");
    p.u_texmat = glGetUniformLocation(program, "xt_TexMatrix0");
    p.u_clip_count = glGetUniformLocation(program, "xt_ClipPlanes");
    p.u_clip_planes = glGetUniformLocation(program, "xt_ClipPlane");
    readInitialUniforms(program, p.initial, p.initial_values);

    int id = ++m_script_next_id;
    m_script_programs[id] = p;
    return id;
}

void RenderGL::scriptShaderFree(int shader)
{
    auto it = m_script_programs.find(shader);
    if(it == m_script_programs.end())
        return;

    glDeleteProgram(it->second.program);
    GLProgramObject::s_forget_active_program();
    m_script_programs.erase(it);
}

void RenderGL::scriptShaderReset(int shader)
{
    auto it = m_script_programs.find(shader);
    if(it == m_script_programs.end())
        return;

    const ScriptProgram_t &p = it->second;
    glUseProgram(p.program);
    for(const auto &u : p.initial)
        setUniformElement(u.location, u.type, &p.initial_values[u.offset]);
    GLProgramObject::s_forget_active_program();
}

void RenderGL::scriptShaderVariables(int shader, bool attributes, std::vector<ScriptShaderVar_t> &out)
{
    out.clear();

    auto it = m_script_programs.find(shader);
    if(it == m_script_programs.end())
        return;

    GLuint program = it->second.program;
    GLint count = 0, max_len = 0;
    glGetProgramiv(program, attributes ? GL_ACTIVE_ATTRIBUTES : GL_ACTIVE_UNIFORMS, &count);
    glGetProgramiv(program, attributes ? GL_ACTIVE_ATTRIBUTE_MAX_LENGTH : GL_ACTIVE_UNIFORM_MAX_LENGTH, &max_len);

    std::vector<char> name((size_t)max_len + 1);
    for(GLint i = 0; i < count; i++)
    {
        GLsizei len = 0;
        GLint size = 0;
        GLenum type = 0;

        if(attributes)
            glGetActiveAttrib(program, (GLuint)i, (GLsizei)name.size(), &len, &size, &type, name.data());
        else
            glGetActiveUniform(program, (GLuint)i, (GLsizei)name.size(), &len, &size, &type, name.data());

        ScriptShaderVar_t v;
        v.name.assign(name.data(), (size_t)len);
        v.location = attributes ? glGetAttribLocation(program, v.name.c_str())
                                : glGetUniformLocation(program, v.name.c_str());
        v.type = type;
        v.size = size;
        out.push_back(std::move(v));
    }
}

int RenderGL::scriptBufferCreate(int w, int h)
{
    if(!scriptShaderHeader() || w <= 0 || h <= 0)
        return 0;

    scriptDrawEnd();

    ScriptBuffer_t b;
    b.w = w;
    b.h = h;

    glGenTextures(1, &b.texture);
    glBindTexture(GL_TEXTURE_2D, b.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &b.fb);
    glBindFramebuffer(GL_FRAMEBUFFER, b.fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, b.texture, 0);
    bool complete = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glBindFramebuffer(GL_FRAMEBUFFER, scriptCurrentFB());

    if(!complete)
    {
        pLogWarning("smbx2compat: couldn't create a %dx%d capture buffer", w, h);
        glDeleteFramebuffers(1, &b.fb);
        glDeleteTextures(1, &b.texture);
        return 0;
    }

    int id = ++m_script_next_id;
    m_script_buffers[id] = b;
    scriptBufferClear(id);
    scriptDrawEnd();
    return id;
}

void RenderGL::scriptBufferFree(int buffer)
{
    auto it = m_script_buffers.find(buffer);
    if(it == m_script_buffers.end())
        return;

    if(m_script_redirect == buffer)
        scriptRedirect(0);
    if(m_script_bound_buffer == buffer)
        scriptDrawEnd();

    glDeleteFramebuffers(1, &it->second.fb);
    glDeleteTextures(1, &it->second.texture);
    if(it->second.depth_rb)
        glDeleteRenderbuffers(1, &it->second.depth_rb);
    m_script_buffers.erase(it);
}

void RenderGL::scriptBufferClear(int buffer)
{
    auto it = m_script_buffers.find(buffer);
    if(it == m_script_buffers.end())
        return;

    scriptBindBuffer(it->first, it->second);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // LunaLua clears depth with the buffer (to the far plane)
    if(it->second.depth_rb)
        scriptClearDepth(1.0f);

    // the engine's (initState)
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

void RenderGL::scriptBufferCapture(int buffer)
{
    auto it = m_script_buffers.find(buffer);
    if(it == m_script_buffers.end() || !scriptCurrentFB() || buffer == m_script_redirect)
        return;

    // reads the engine's framebuffer
    scriptDrawEnd();
    flushDrawQueues();

    // LunaLua copies the top-left of the camera's view
    RectSizeI r = m_viewport;
    s_normalize_coords(r);
    int w = SDL_min(it->second.w, r.w);
    int h = SDL_min(it->second.h, r.h);
    if(w <= 0 || h <= 0)
        return;

    // a redirect's capture buffer holds the view at its top-left, unscaled
    float scale = m_render_scale_factor;
    if(m_script_redirect)
    {
        r.x -= m_viewport.x;
        r.y -= m_viewport.y;
        scale = 1.0f;
    }

    r = r * scale;

    glBindTexture(GL_TEXTURE_2D, it->second.texture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, r.x, r.y,
                        (GLsizei)(w * scale), (GLsizei)(h * scale));
}

void RenderGL::scriptDraw(const ScriptDraw_t &d)
{
    static bool warned_type = false, warned_attr = false;

    if(d.count <= 0 || !d.xy || d.primitive > GL_TRIANGLE_FAN)
        return;

    auto pit = m_script_programs.find(d.shader);
    if(pit == m_script_programs.end())
        return;

    const ScriptProgram_t &prog = pit->second;

    ScriptBuffer_t *target = nullptr;
    if(d.target)
    {
        auto bit = m_script_buffers.find(d.target);
        if(bit == m_script_buffers.end())
            return;
        target = &bit->second;
    }
    else
    {
        scriptDrawEnd();
        if(!scriptCurrentFB())
            return;
    }

    // resolves a script texture; scale: 0..1 of the picture -> GL texture coordinates
    auto textureOf = [this, target](const ScriptTexture_t &t, GLfloat *scale) -> GLuint
    {
        if(scale)
            scale[0] = scale[1] = 1.0f;

        if(t.picture)
        {
            StdPicture *tx = t.picture;
            if(!tx->inited)
                return 0;
            if(!tx->d.texture_id && tx->l.lazyLoaded)
                lazyLoad(*tx);
            if(scale)
            {
                scale[0] = tx->w * tx->d.w_scale;
                scale[1] = tx->h * tx->d.h_scale;
            }
            return tx->d.texture_id;
        }

        if(t.buffer)
        {
            auto bit = m_script_buffers.find(t.buffer);
            // a buffer can't be read while it's drawn into
            if(bit == m_script_buffers.end() || &bit->second == target || (!target && t.buffer == m_script_redirect))
                return 0;
            return bit->second.texture;
        }

        return 0;
    };

    if(!m_script_white_texture)
    {
        const GLubyte white[4] = {255, 255, 255, 255};
        glGenTextures(1, &m_script_white_texture);
        glBindTexture(GL_TEXTURE_2D, m_script_white_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        GLint units = 8;
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
        m_script_first_unit = TEXTURE_UNIT_LIGHT_READ - GL_TEXTURE0 + 1;
        m_script_num_units = SDL_min(SDL_max(units - m_script_first_unit, 0), 8);
    }

    if(!m_script_vbo)
        glGenBuffers(1, &m_script_vbo);

    // everything queued so far goes below a draw to the screen (a draw into a
    // capture buffer doesn't touch the screen)
    if(target)
        scriptBindBuffer(d.target, *target);
    else
        flushDrawQueues();

    // a new depth buffer starts at the far plane, as if cleared with the buffer
    bool depth_test = false;
    if(d.depthTest && target)
    {
        bool fresh = !target->depth_rb;
        depth_test = scriptAddDepth(*target);
        if(depth_test && fresh)
            scriptClearDepth(1.0f);
    }

    // matrices: P maps the view to clip space, MV moves scene coordinates into the view
    Mat4 proj;
    if(target)
    {
        proj = {2.0f / target->w, 0.0f, 0.0f, 0.0f,
                0.0f, 2.0f / target->h, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                -1.0f, -1.0f, 0.0f, 1.0f};
    }
    else
        proj = m_transform_matrix;

    Mat4 mv = mat4Translate(d.offsetX, d.offsetY);

    GLfloat tex_scale[2];
    GLuint tex0 = textureOf(d.texture, tex_scale);
    if(!tex0)
        tex0 = m_script_white_texture;

    glUseProgram(prog.program);
    GLProgramObject::s_forget_active_program();

    if(prog.u_mvp != -1)
        glUniformMatrix4fv(prog.u_mvp, 1, GL_FALSE, mat4Multiply(proj, mv).data());
    if(prog.u_mv != -1)
        glUniformMatrix4fv(prog.u_mv, 1, GL_FALSE, mv.data());
    if(prog.u_p != -1)
        glUniformMatrix4fv(prog.u_p, 1, GL_FALSE, proj.data());
    if(prog.u_texmat != -1)
        glUniformMatrix4fv(prog.u_texmat, 1, GL_FALSE, mat4Scale(tex_scale[0], tex_scale[1]).data());
    if(prog.u_clip_count != -1)
    {
        int planes = SDL_min(SDL_max(d.clipPlaneCount, 0), 6);
        glUniform1i(prog.u_clip_count, planes);
        if(planes > 0 && prog.u_clip_planes != -1)
            glUniform4fv(prog.u_clip_planes, planes, &d.clipPlanes[0][0]);
    }

    // uniforms (samplers get texture units after the engine's)
    int next_unit = 0;
    for(int i = 0; i < d.uniformCount; i++)
    {
        const ScriptDrawVar_t &u = d.uniforms[i];
        if(u.location < 0 || u.count <= 0 || !u.data)
            continue;

        const GLfloat *f = static_cast<const GLfloat *>(u.data);
        const GLint *iv = static_cast<const GLint *>(u.data);

        switch(u.type)
        {
        case GL_FLOAT:        glUniform1fv(u.location, u.count, f); break;
        case GL_FLOAT_VEC2:   glUniform2fv(u.location, u.count, f); break;
        case GL_FLOAT_VEC3:   glUniform3fv(u.location, u.count, f); break;
        case GL_FLOAT_VEC4:   glUniform4fv(u.location, u.count, f); break;
        case GL_FLOAT_MAT2:   glUniformMatrix2fv(u.location, u.count, GL_FALSE, f); break;
        case GL_FLOAT_MAT3:   glUniformMatrix3fv(u.location, u.count, GL_FALSE, f); break;
        case GL_FLOAT_MAT4:   glUniformMatrix4fv(u.location, u.count, GL_FALSE, f); break;
        case GL_INT:          glUniform1iv(u.location, u.count, iv); break;
        case GL_INT_VEC2:     glUniform2iv(u.location, u.count, iv); break;
        case GL_INT_VEC3:     glUniform3iv(u.location, u.count, iv); break;
        case GL_INT_VEC4:     glUniform4iv(u.location, u.count, iv); break;
#ifdef THEXTECH_BUILD_GL_DESKTOP_MODERN
        case GL_UNSIGNED_INT:      glUniform1uiv(u.location, u.count, static_cast<const GLuint *>(u.data)); break;
        case GL_UNSIGNED_INT_VEC2: glUniform2uiv(u.location, u.count, static_cast<const GLuint *>(u.data)); break;
        case GL_UNSIGNED_INT_VEC3: glUniform3uiv(u.location, u.count, static_cast<const GLuint *>(u.data)); break;
        case GL_UNSIGNED_INT_VEC4: glUniform4uiv(u.location, u.count, static_cast<const GLuint *>(u.data)); break;
        case GL_FLOAT_MAT2x3: glUniformMatrix2x3fv(u.location, u.count, GL_FALSE, f); break;
        case GL_FLOAT_MAT2x4: glUniformMatrix2x4fv(u.location, u.count, GL_FALSE, f); break;
        case GL_FLOAT_MAT3x2: glUniformMatrix3x2fv(u.location, u.count, GL_FALSE, f); break;
        case GL_FLOAT_MAT3x4: glUniformMatrix3x4fv(u.location, u.count, GL_FALSE, f); break;
        case GL_FLOAT_MAT4x2: glUniformMatrix4x2fv(u.location, u.count, GL_FALSE, f); break;
        case GL_FLOAT_MAT4x3: glUniformMatrix4x3fv(u.location, u.count, GL_FALSE, f); break;
#endif
        case GL_SAMPLER_2D:
        {
            const ScriptTexture_t *t = static_cast<const ScriptTexture_t *>(u.data);
            GLint units[8];
            int n = SDL_min(u.count, 8);
            for(int k = 0; k < n; k++)
            {
                GLuint name = textureOf(t[k], nullptr);
                if(next_unit < m_script_num_units)
                {
                    units[k] = m_script_first_unit + next_unit++;
                    glActiveTexture(GL_TEXTURE0 + units[k]);
                    glBindTexture(GL_TEXTURE_2D, name ? name : m_script_white_texture);
                }
                else
                    units[k] = 0;
            }
            glActiveTexture(GL_TEXTURE0);
            glUniform1iv(u.location, n, units);
            break;
        }
        default:
            warnOnce(warned_type, "a shader uniform type isn't supported yet (not set)", u.type);
            break;
        }
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex0);
    if(d.linearFiltered)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    // vertex arrays, one after another in one buffer
    size_t n = (size_t)d.count;
    std::vector<GLfloat> &data = m_script_vertex_data;
    data.assign(d.xy, d.xy + 2 * n);

    size_t uv_at = data.size();
    if(d.uv)
        data.insert(data.end(), d.uv, d.uv + 2 * n);

    size_t rgba_at = data.size();
    if(d.rgba)
        data.insert(data.end(), d.rgba, d.rgba + 4 * n);

    std::vector<size_t> attr_at((size_t)d.attributeCount, 0);
    for(int i = 0; i < d.attributeCount; i++)
    {
        const ScriptDrawVar_t &a = d.attributes[i];
        int comps = floatComponents(a.type);
        attr_at[i] = data.size();
        if(comps && a.location > 2 && a.data && a.count >= d.count)
        {
            const GLfloat *f = static_cast<const GLfloat *>(a.data);
            data.insert(data.end(), f, f + comps * n);
        }
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_script_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(data.size() * sizeof(GLfloat)), data.data(), GL_STREAM_DRAW);

    auto offset = [](size_t at) { return reinterpret_cast<const void *>(at * sizeof(GLfloat)); };

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, offset(0));

    if(d.uv)
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, offset(uv_at));
    else
    {
        glDisableVertexAttribArray(1);
        glVertexAttrib4f(1, 0.0f, 0.0f, 0.0f, 1.0f);
    }

    if(d.rgba)
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 0, offset(rgba_at));
    else
    {
        // LunaLua: glColor4f with the colour premultiplied
        glDisableVertexAttribArray(2);
        glVertexAttrib4f(2, d.color[0] * d.color[3], d.color[1] * d.color[3], d.color[2] * d.color[3], d.color[3]);
    }

    for(int i = 0; i < d.attributeCount; i++)
    {
        const ScriptDrawVar_t &a = d.attributes[i];
        int comps = floatComponents(a.type);
        if(a.location <= 2)
            continue;

        if(!comps || !a.data || a.count < d.count)
        {
            warnOnce(warned_attr, "a shader attribute type isn't supported yet (not set)", a.type);
            continue;
        }

        glEnableVertexAttribArray((GLuint)a.location);
        glVertexAttribPointer((GLuint)a.location, comps, GL_FLOAT, GL_FALSE, 0, offset(attr_at[i]));
    }

    // LunaLua's state for these draws
    GLboolean depth_mask = GL_TRUE;
    if(depth_test)
    {
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_TRUE);
    }
    else if(m_use_depth_buffer)
        glDisable(GL_DEPTH_TEST);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glDrawArrays((GLenum)d.primitive, 0, (GLsizei)d.count);

    // the engine's state again (render_gl_init.cpp)
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if(depth_test)
    {
        glDepthFunc(GL_GEQUAL);
        glDepthMask(depth_mask);
        if(!m_use_depth_buffer)
            glDisable(GL_DEPTH_TEST);
    }
    else if(m_use_depth_buffer)
        glEnable(GL_DEPTH_TEST);

    for(int i = 0; i < d.attributeCount; i++)
    {
        const ScriptDrawVar_t &a = d.attributes[i];
        if(a.location > 2 && floatComponents(a.type) && a.data && a.count >= d.count)
            glDisableVertexAttribArray((GLuint)a.location);
    }

    if(!d.uv)
        glEnableVertexAttribArray(1);
    if(!d.rgba)
        glEnableVertexAttribArray(2);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    if(d.linearFiltered)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
}

void RenderGL::scriptCleanup()
{
    scriptRedirect(0);
    scriptDrawEnd();

    for(auto &p : m_script_programs)
        glDeleteProgram(p.second.program);
    m_script_programs.clear();

    for(auto &b : m_script_buffers)
    {
        glDeleteFramebuffers(1, &b.second.fb);
        glDeleteTextures(1, &b.second.texture);
        if(b.second.depth_rb)
            glDeleteRenderbuffers(1, &b.second.depth_rb);
    }
    m_script_buffers.clear();

    if(m_script_vbo)
    {
        glDeleteBuffers(1, &m_script_vbo);
        m_script_vbo = 0;
    }

    if(m_script_white_texture)
    {
        glDeleteTextures(1, &m_script_white_texture);
        m_script_white_texture = 0;
    }

    GLProgramObject::s_forget_active_program();
}

#else // shaders or framebuffers unavailable: as AbstractRender_t

GLuint RenderGL::scriptCurrentFB() const
{
    return 0;
}

const char *RenderGL::scriptShaderHeader()
{
    return AbstractRender_t::scriptShaderHeader();
}

int RenderGL::scriptShaderCreate(const char *vertex, const char *fragment, std::string &error)
{
    return AbstractRender_t::scriptShaderCreate(vertex, fragment, error);
}

void RenderGL::scriptShaderFree(int shader)
{
    AbstractRender_t::scriptShaderFree(shader);
}

void RenderGL::scriptShaderReset(int shader)
{
    AbstractRender_t::scriptShaderReset(shader);
}

void RenderGL::scriptShaderVariables(int shader, bool attributes, std::vector<ScriptShaderVar_t> &out)
{
    AbstractRender_t::scriptShaderVariables(shader, attributes, out);
}

int RenderGL::scriptBufferCreate(int w, int h)
{
    return AbstractRender_t::scriptBufferCreate(w, h);
}

void RenderGL::scriptBufferFree(int buffer)
{
    AbstractRender_t::scriptBufferFree(buffer);
}

void RenderGL::scriptBufferClear(int buffer)
{
    AbstractRender_t::scriptBufferClear(buffer);
}

void RenderGL::scriptBufferCapture(int buffer)
{
    AbstractRender_t::scriptBufferCapture(buffer);
}

void RenderGL::scriptDraw(const ScriptDraw_t &draw)
{
    AbstractRender_t::scriptDraw(draw);
}

void RenderGL::scriptDrawEnd()
{
}

void RenderGL::scriptRedirect(int buffer)
{
    AbstractRender_t::scriptRedirect(buffer);
}

void RenderGL::scriptCleanup()
{
}

#endif

void RenderGL::scriptStats(int &textures, int &programs, int &buffers)
{
    textures = (int)m_loadedPictures.size();
    programs = (int)m_script_programs.size();
    buffers = (int)m_script_buffers.size();
}
