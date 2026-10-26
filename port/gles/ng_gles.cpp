/* NoGravity GLES 3 backend for the RLX OpenGL renderer. See ng_gles.h. */
#include "ng_gles.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#undef glBegin
#undef glEnd
#undef glEnable
#undef glDisable
#undef glTexParameteri
#undef glTexImage2D
#undef glTexSubImage2D
#undef glGetIntegerv
#undef glReadPixels
#undef glViewport
#undef glClear
#undef glFinish
#undef glBindTexture
#undef glGetString

#ifdef __ANDROID__
#include <android/log.h>
#define NGLOG(...) __android_log_print(ANDROID_LOG_INFO, "NoGravityVR", __VA_ARGS__)
#else
#define NGLOG(...) (fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))
#endif

namespace {

struct Vtx { float x, y, z, w, s, t, r, g, b, a; };

GLuint s_prog = 0, s_pixTex = 0;
GLint  s_uMvp = -1, s_uMode = -1, s_uTex = -1;
bool   s_inited = false;

float  s_proj[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
float  s_mv[16]   = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
float *s_curMat = s_proj;

bool   s_tex2D = false;
GLint  s_texEnv = GL_MODULATE;
float  s_col[4] = {1, 1, 1, 1};
float  s_tc[4] = {0, 0, 0, 1};
GLenum s_mode = 0;
std::vector<Vtx> s_verts, s_tris;

GLuint s_targetFbo = 0;

struct TexFlags { GLuint id; bool mip; };
std::vector<TexFlags> s_texFlags;

struct Attr {
    GLboolean blend, depth, cull, dither, depthMask;
    GLint src, dst, depthFunc, tex;
    bool tex2D; GLint texEnv;
};
std::vector<Attr> s_attrStack;

void identity(float *m) { memset(m, 0, 64); m[0] = m[5] = m[10] = m[15] = 1.f; }
void mul(const float *a, const float *b, float *o) {
    float r[16];
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++)
            r[c * 4 + rr] = a[0 * 4 + rr] * b[c * 4 + 0] + a[1 * 4 + rr] * b[c * 4 + 1] + a[2 * 4 + rr] * b[c * 4 + 2] + a[3 * 4 + rr] * b[c * 4 + 3];
    memcpy(o, r, 64);
}

GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof log, NULL, log); NGLOG("[NGL] shader error: %s", log); }
    return s;
}

const char *kVS =
    "#version 300 es\n"
    "layout(location=0) in vec4 a_pos;\n"
    "layout(location=1) in vec2 a_uv;\n"
    "layout(location=2) in vec4 a_col;\n"
    "uniform mat4 u_mvp;\n"
    "out vec2 v_uv; out vec4 v_col;\n"
    "void main(){ v_uv = a_uv; v_col = a_col; gl_Position = u_mvp * a_pos; }\n";
const char *kFS =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec2 v_uv; in vec4 v_col;\n"
    "uniform sampler2D u_tex; uniform int u_mode;\n"
    "out vec4 o;\n"
    "void main(){\n"
    "  if (u_mode == 0) { o = v_col; return; }\n"
    "  vec4 t = texture(u_tex, v_uv);\n"
    "  if (u_mode == 1) o = t * v_col;\n"
    "  else if (u_mode == 2) o = t;\n"
    "  else o = vec4(mix(v_col.rgb, t.rgb, t.a), v_col.a);\n"
    "}\n";

void ensureInit() {
    if (s_inited) return;
    s_inited = true;
    s_prog = glCreateProgram();
    glAttachShader(s_prog, compile(GL_VERTEX_SHADER, kVS));
    glAttachShader(s_prog, compile(GL_FRAGMENT_SHADER, kFS));
    glLinkProgram(s_prog);
    GLint ok = 0;
    glGetProgramiv(s_prog, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(s_prog, sizeof log, NULL, log); NGLOG("[NGL] link error: %s", log); }
    s_uMvp = glGetUniformLocation(s_prog, "u_mvp");
    s_uMode = glGetUniformLocation(s_prog, "u_mode");
    s_uTex = glGetUniformLocation(s_prog, "u_tex");
    NGLOG("[NGL] GLES backend: %s | %s", (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER));
}

TexFlags *flagsFor(GLuint id) {
    for (auto &t : s_texFlags) if (t.id == id) return &t;
    s_texFlags.push_back({id, false});
    return &s_texFlags.back();
}
GLuint boundTex() { GLint t = 0; ::glGetIntegerv(GL_TEXTURE_BINDING_2D, &t); return (GLuint)t; }

void draw(GLenum prim, const Vtx *v, int n) {
    if (n <= 0) return;
    ensureInit();
    glUseProgram(s_prog);
    float mvp[16];
    mul(s_proj, s_mv, mvp);
    glUniformMatrix4fv(s_uMvp, 1, GL_FALSE, mvp);
    int mode = 0;
    if (s_tex2D && boundTex() != 0)
        mode = (s_texEnv == GL_REPLACE) ? 2 : (s_texEnv == GL_DECAL ? 3 : 1);
    glUniform1i(s_uMode, mode);
    glUniform1i(s_uTex, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), &v->x);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), &v->s);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), &v->r);
    glDrawArrays(prim, 0, n);
    static int dbg = -1;
    if (dbg < 0) { const char *e = getenv("NGL_DEBUG"); dbg = (e && *e == '1') ? 400 : 0; }
    if (dbg > 0) {
        dbg--;
        GLenum err = glGetError();
        GLint vp[4]; ::glGetIntegerv(GL_VIEWPORT, vp);
        GLint fb = 0; ::glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
        NGLOG("[NGL] draw prim=%d n=%d mode=%d tex=%u err=0x%x vp=%d,%d,%d,%d fb=%d v0=(%.1f,%.1f,%.3f,%.3f) uv=(%.3f,%.3f) col=(%.2f,%.2f,%.2f,%.2f) blend=%d depth=%d",
              (int)prim, n, mode, boundTex(), err, vp[0], vp[1], vp[2], vp[3], fb, v->x, v->y, v->z, v->w, v->s, v->t, v->r, v->g, v->b, v->a,
              (int)glIsEnabled(GL_BLEND), (int)glIsEnabled(GL_DEPTH_TEST));
        if (dbg > 390) for (int k = 0; k < n; k++) NGLOG("[NGL]   v%d (%.2f,%.2f,%.4f,%.4f) uv(%.4f,%.4f)", k, v[k].x, v[k].y, v[k].z, v[k].w, v[k].s, v[k].t);
    }
}

/* convert any supported source format to tightly packed RGBA8 */
const void *toRGBA(GLenum format, GLenum type, GLsizei w, GLsizei h, const void *src, std::vector<unsigned char> &buf,
                   GLenum *outFmt, GLenum *outType, GLint *outIfmt) {
    *outType = GL_UNSIGNED_BYTE;
    if (!src) {
        *outFmt = GL_RGBA; *outIfmt = GL_RGBA8;
        return NULL;
    }
    size_t n = (size_t)w * h;
    const unsigned char *s = (const unsigned char *)src;
    if ((format == GL_BGRA_EXT) && (type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_INT_8_8_8_8_REV)) {
        buf.resize(n * 4);
        for (size_t i = 0; i < n; i++) { buf[i*4] = s[i*4+2]; buf[i*4+1] = s[i*4+1]; buf[i*4+2] = s[i*4]; buf[i*4+3] = s[i*4+3]; }
        *outFmt = GL_RGBA; *outIfmt = GL_RGBA8; return buf.data();
    }
    if (format == GL_BGR_EXT && type == GL_UNSIGNED_BYTE) {
        buf.resize(n * 4);
        for (size_t i = 0; i < n; i++) { buf[i*4] = s[i*3+2]; buf[i*4+1] = s[i*3+1]; buf[i*4+2] = s[i*3]; buf[i*4+3] = 255; }
        *outFmt = GL_RGBA; *outIfmt = GL_RGBA8; return buf.data();
    }
    if (format == GL_RGB && type == GL_UNSIGNED_BYTE) {
        buf.resize(n * 4);
        for (size_t i = 0; i < n; i++) { buf[i*4] = s[i*3]; buf[i*4+1] = s[i*3+1]; buf[i*4+2] = s[i*3+2]; buf[i*4+3] = 255; }
        *outFmt = GL_RGBA; *outIfmt = GL_RGBA8; return buf.data();
    }
    if (format == GL_RGBA && type == GL_UNSIGNED_BYTE) { *outFmt = GL_RGBA; *outIfmt = GL_RGBA8; return src; }
    if (format == GL_RGB && type == GL_UNSIGNED_SHORT_5_6_5) { *outFmt = GL_RGB; *outType = type; *outIfmt = GL_RGB565; return src; }
    if (format == GL_RGBA && (type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1)) {
        *outFmt = GL_RGBA; *outType = type; *outIfmt = type == GL_UNSIGNED_SHORT_4_4_4_4 ? GL_RGBA4 : GL_RGB5_A1; return src;
    }
    static int warned = 0;
    if (warned++ < 5) NGLOG("[NGL] unsupported texture format 0x%04X type 0x%04X", format, type);
    *outFmt = GL_RGBA; *outIfmt = GL_RGBA8;
    buf.assign(n * 4, 255);
    return buf.data();
}

} // namespace

extern "C" {

void NGL_Init(void) { ensureInit(); }

void NGL_BindTexture(GLenum target, GLuint tex) {
    if (target == GL_TEXTURE_RECTANGLE_ARB) target = GL_TEXTURE_2D;
    glBindTexture(target, tex);
}

/* Hide desktop-only extensions the renderer would otherwise use (rectangle textures,
   paletted textures, S3TC): ES 3 has none of them, the power-of-two path works everywhere. */
const GLubyte *NGL_GetString(GLenum name) {
    if (name == GL_EXTENSIONS) return (const GLubyte *)"GL_ARB_multitexture";
    return glGetString(name);
}

void NGL_SetTargetFramebuffer(GLuint fbo, int w, int h) {
    s_targetFbo = fbo;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    (void)w; (void)h;
}

/* VR eye pass: draw the engine's pixel coordinates into a w x h target */
static float s_savedProj[16];
static GLint s_savedVp[4];
void NGL_PushScreen(int w, int h) {
    memcpy(s_savedProj, s_proj, sizeof s_savedProj);
    ::glGetIntegerv(GL_VIEWPORT, s_savedVp);
    identity(s_proj);
    s_proj[0] = 2.f / (float)w;
    s_proj[5] = -2.f / (float)h;
    s_proj[10] = -1.f;
    s_proj[12] = -1.f;
    s_proj[13] = 1.f;
    glViewport(0, 0, w, h);
}
void NGL_PopScreen(void) {
    memcpy(s_proj, s_savedProj, sizeof s_savedProj);
    glViewport(s_savedVp[0], s_savedVp[1], s_savedVp[2], s_savedVp[3]);
}

void NGL_Begin(GLenum mode) { s_mode = mode; s_verts.clear(); }

void NGL_Vertex3f(GLfloat x, GLfloat y, GLfloat z) {
    float q = s_tc[3];
    float w = (q != 0.f && q != 1.f) ? 1.f / q : 1.f;
    Vtx v = { x * w, y * w, z * w, w, s_tc[0] * w, s_tc[1] * w, s_col[0], s_col[1], s_col[2], s_col[3] };
    s_verts.push_back(v);
}

void NGL_TexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q) { s_tc[0] = s; s_tc[1] = t; s_tc[2] = r; s_tc[3] = q; }
void NGL_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { s_col[0] = r; s_col[1] = g; s_col[2] = b; s_col[3] = a; }

void NGL_End(void) {
    const int n = (int)s_verts.size();
    switch (s_mode) {
    case GL_QUADS:
        s_tris.clear();
        for (int i = 0; i + 3 < n; i += 4) {
            const Vtx *q = &s_verts[i];
            s_tris.push_back(q[0]); s_tris.push_back(q[1]); s_tris.push_back(q[2]);
            s_tris.push_back(q[0]); s_tris.push_back(q[2]); s_tris.push_back(q[3]);
        }
        draw(GL_TRIANGLES, s_tris.data(), (int)s_tris.size());
        break;
    case 0x0009: /* GL_POLYGON */
        draw(GL_TRIANGLE_FAN, s_verts.data(), n);
        break;
    default:
        draw(s_mode, s_verts.data(), n);
        break;
    }
    s_verts.clear();
    s_tc[3] = 1.f;
}

void NGL_MatrixMode(GLenum mode) { s_curMat = (mode == GL_PROJECTION) ? s_proj : s_mv; }
void NGL_LoadIdentity(void) { identity(s_curMat); }
void NGL_Ortho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f) {
    float o[16];
    identity(o);
    o[0] = (float)(2.0 / (r - l));
    o[5] = (float)(2.0 / (t - b));
    o[10] = (float)(-2.0 / (f - n));
    o[12] = (float)(-(r + l) / (r - l));
    o[13] = (float)(-(t + b) / (t - b));
    o[14] = (float)(-(f + n) / (f - n));
    mul(s_curMat, o, s_curMat);
}

void NGL_Enable(GLenum cap) {
    switch (cap) {
    case GL_TEXTURE_2D: case GL_TEXTURE_RECTANGLE_ARB: s_tex2D = true; break;
    case GL_LIGHTING: case GL_MULTISAMPLE_ARB: break;
    default: glEnable(cap); break;
    }
}
void NGL_Disable(GLenum cap) {
    switch (cap) {
    case GL_TEXTURE_2D: case GL_TEXTURE_RECTANGLE_ARB: s_tex2D = false; break;
    case GL_LIGHTING: case GL_MULTISAMPLE_ARB: break;
    default: glDisable(cap); break;
    }
}
void NGL_TexEnvi(GLenum target, GLenum pname, GLint param) {
    if (target == GL_TEXTURE_ENV && pname == GL_TEXTURE_ENV_MODE) s_texEnv = param;
}

void NGL_PushAttrib(GLbitfield) {
    Attr a;
    a.blend = glIsEnabled(GL_BLEND); a.depth = glIsEnabled(GL_DEPTH_TEST); a.cull = glIsEnabled(GL_CULL_FACE);
    a.dither = glIsEnabled(GL_DITHER);
    ::glGetIntegerv(GL_BLEND_SRC_RGB, &a.src); ::glGetIntegerv(GL_BLEND_DST_RGB, &a.dst);
    ::glGetIntegerv(GL_DEPTH_FUNC, &a.depthFunc);
    GLboolean dm; glGetBooleanv(GL_DEPTH_WRITEMASK, &dm); a.depthMask = dm;
    a.tex = (GLint)boundTex(); a.tex2D = s_tex2D; a.texEnv = s_texEnv;
    s_attrStack.push_back(a);
}
void NGL_PopAttrib(void) {
    if (s_attrStack.empty()) return;
    Attr a = s_attrStack.back(); s_attrStack.pop_back();
    (a.blend ? glEnable : glDisable)(GL_BLEND);
    (a.depth ? glEnable : glDisable)(GL_DEPTH_TEST);
    (a.cull ? glEnable : glDisable)(GL_CULL_FACE);
    (a.dither ? glEnable : glDisable)(GL_DITHER);
    glBlendFunc(a.src, a.dst);
    glDepthFunc(a.depthFunc);
    glDepthMask(a.depthMask);
    glBindTexture(GL_TEXTURE_2D, a.tex);
    s_tex2D = a.tex2D; s_texEnv = a.texEnv;
}
void NGL_PolygonMode(GLenum, GLenum) {}

void NGL_TexParameteri(GLenum target, GLenum pname, GLint param) {
    if (target == GL_TEXTURE_RECTANGLE_ARB) target = GL_TEXTURE_2D;
    if (pname == GL_GENERATE_MIPMAP) { flagsFor(boundTex())->mip = param != 0; return; }
    if ((pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T) && param == GL_CLAMP) param = GL_CLAMP_TO_EDGE;
    glTexParameteri(target, pname, param);
}

void NGL_TexImage2D(GLenum target, GLint level, GLint, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type,
                    const void *pixels) {
    std::vector<unsigned char> buf;
    GLenum f, t; GLint ifmt;
    const void *p = toRGBA(format, type, w, h, pixels, buf, &f, &t, &ifmt);
    if (target == GL_TEXTURE_RECTANGLE_ARB) target = GL_TEXTURE_2D;
    GLint align; ::glGetIntegerv(GL_UNPACK_ALIGNMENT, &align);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(target, level, ifmt, w, h, border, f, t, p);
    glPixelStorei(GL_UNPACK_ALIGNMENT, align);
    if (level == 0 && flagsFor(boundTex())->mip) glGenerateMipmap(GL_TEXTURE_2D);
    if (getenv("NGL_DEBUG")) NGLOG("[NGL] teximage tex=%u %dx%d fmt=0x%x type=0x%x -> 0x%x err=0x%x", boundTex(), w, h, format, type, ifmt, glGetError());
}

void NGL_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type,
                       const void *pixels) {
    std::vector<unsigned char> buf;
    GLenum f, t; GLint ifmt;
    const void *p = toRGBA(format, type, w, h, pixels, buf, &f, &t, &ifmt);
    if (target == GL_TEXTURE_RECTANGLE_ARB) target = GL_TEXTURE_2D;
    GLint align; ::glGetIntegerv(GL_UNPACK_ALIGNMENT, &align);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(target, level, x, y, w, h, f, t, p);
    glPixelStorei(GL_UNPACK_ALIGNMENT, align);
    if (level == 0 && flagsFor(boundTex())->mip) glGenerateMipmap(GL_TEXTURE_2D);
}

void NGL_GetIntegerv(GLenum pname, GLint *v) {
    if (pname == GL_MAX_TEXTURE_UNITS) { *v = 1; return; }
    glGetIntegerv(pname, v);
}

void NGL_ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *pixels) {
    if (format == GL_RGBA && type == GL_UNSIGNED_BYTE) { glReadPixels(x, y, w, h, format, type, pixels); return; }
    std::vector<unsigned char> tmp((size_t)w * h * 4);
    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, tmp.data());
    unsigned char *d = (unsigned char *)pixels;
    for (size_t i = 0; i < (size_t)w * h; i++) { d[i*3] = tmp[i*4]; d[i*3+1] = tmp[i*4+1]; d[i*3+2] = tmp[i*4+2]; }
}

void NGL_DrawPixels(GLsizei w, GLsizei h, GLenum format, GLenum type, const void *pixels) {
    if (format != GL_RGB && format != GL_RGBA) return; /* depth restore: unsupported, ignored */
    ensureInit();
    std::vector<unsigned char> buf;
    GLenum f, t; GLint ifmt;
    const void *p = toRGBA(format, type, w, h, pixels, buf, &f, &t, &ifmt);
    GLuint prev = boundTex();
    if (!s_pixTex) {
        glGenTextures(1, &s_pixTex);
        glBindTexture(GL_TEXTURE_2D, s_pixTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else glBindTexture(GL_TEXTURE_2D, s_pixTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, f, t, p);
    /* window-space quad at the origin (GL raster position 0,0 = bottom left) */
    float sp[16], smv[16];
    memcpy(sp, s_proj, 64); memcpy(smv, s_mv, 64);
    identity(s_proj); identity(s_mv);
    GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
    float x1 = 2.f * w / vp[2] - 1.f, y1 = 2.f * h / vp[3] - 1.f;
    Vtx q[6] = {
        {-1, -1, 0, 1, 0, 0, 1, 1, 1, 1}, {x1, -1, 0, 1, 1, 0, 1, 1, 1, 1}, {x1, y1, 0, 1, 1, 1, 1, 1, 1, 1},
        {-1, -1, 0, 1, 0, 0, 1, 1, 1, 1}, {x1, y1, 0, 1, 1, 1, 1, 1, 1, 1}, {-1, y1, 0, 1, 0, 1, 1, 1, 1, 1},
    };
    bool t2 = s_tex2D; GLint te = s_texEnv;
    s_tex2D = true; s_texEnv = GL_REPLACE;
    GLboolean depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
    draw(GL_TRIANGLES, q, 6);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (blend) glEnable(GL_BLEND);
    s_tex2D = t2; s_texEnv = te;
    memcpy(s_proj, sp, 64); memcpy(s_mv, smv, 64);
    glBindTexture(GL_TEXTURE_2D, prev);
}

void NGL_Viewport(GLint x, GLint y, GLsizei w, GLsizei h) { glViewport(x, y, w, h); }

void NGL_Clear(GLbitfield mask) {
    if (s_targetFbo) glBindFramebuffer(GL_FRAMEBUFFER, s_targetFbo);
    glClear(mask);
}

void NGL_Finish(void) { /* glFinish stalls the Adreno pipeline; the swap/present syncs anyway */ }

} // extern "C"
