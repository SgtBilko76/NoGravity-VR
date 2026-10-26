/* NoGravity GLES 3 backend: the RLX OpenGL renderer (gl_gx.cpp / gl_v3x.cpp) uses a small
 * slice of OpenGL 1.x - immediate mode with CPU-projected vertices, a 2D ortho matrix,
 * blend/depth state, MODULATE/REPLACE texenv and BGR(A) texture uploads. This header maps
 * exactly that slice onto GLES 3 (one shader, client-side vertex arrays).
 *
 * Perspective-correct texturing: the engine sends glTexCoord4f(u/w, v/w, ?, 1/w). The vertex
 * is emitted as (x*w, y*w, z*w, w) with uv (u, v), so the GPU's perspective-correct
 * interpolation does the rest (the ortho matrix is affine, so the screen position is kept).
 */
#pragma once

#ifndef GL_GLES_PROTOTYPES
#define GL_GLES_PROTOTYPES 1
#endif
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

/* desktop-only enums the renderer references */
#define GL_QUADS                       0x0007
#define GL_MODELVIEW                   0x1700
#define GL_PROJECTION                  0x1701
#define GL_LIGHTING                    0x0B50
#define GL_TEXTURE_ENV                 0x2300
#define GL_TEXTURE_ENV_MODE            0x2200
#define GL_MODULATE                    0x2100
#define GL_DECAL                       0x2101
#define GL_CLAMP                       0x2900
#define GL_LINE                        0x1B01
#define GL_FILL                        0x1B02
#define GL_FLAT                        0x1D00
#define GL_SMOOTH                      0x1D01
#define GL_PERSPECTIVE_CORRECTION_HINT 0x0C50
#define GL_ALL_ATTRIB_BITS             0x000FFFFF
#define GL_BGR_EXT                     0x80E0
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT                    0x80E1
#endif
#define GL_COLOR_INDEX                 0x1900
#define GL_COLOR_INDEX8_EXT            0x80E5
#define GL_UNSIGNED_INT_8_8_8_8_REV    0x8367
#define GL_TEXTURE_RECTANGLE_ARB       0x84F5
#define GL_GENERATE_MIPMAP             0x8191
#define GL_MAX_TEXTURE_UNITS           0x84E2
#define GL_UNPACK_CLIENT_STORAGE_APPLE 0x85B2
#define GL_MULTISAMPLE_ARB             0x809D
#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT  0x83F0
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#define GL_VERSION_1_2 1
#define GL_VERSION_1_3 1
#define GL_VERSION_1_4 1
typedef double GLdouble;

#ifdef __cplusplus
extern "C" {
#endif
void NGL_Init(void);
void NGL_Begin(GLenum mode);
void NGL_End(void);
void NGL_Vertex3f(GLfloat x, GLfloat y, GLfloat z);
void NGL_TexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q);
void NGL_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void NGL_MatrixMode(GLenum mode);
void NGL_LoadIdentity(void);
void NGL_Ortho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void NGL_Enable(GLenum cap);
void NGL_Disable(GLenum cap);
void NGL_TexEnvi(GLenum target, GLenum pname, GLint param);
void NGL_PushAttrib(GLbitfield mask);
void NGL_PopAttrib(void);
void NGL_PolygonMode(GLenum face, GLenum mode);
void NGL_TexParameteri(GLenum target, GLenum pname, GLint param);
void NGL_TexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint border,
                    GLenum format, GLenum type, const void *pixels);
void NGL_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                       GLenum format, GLenum type, const void *pixels);
void NGL_GetIntegerv(GLenum pname, GLint *v);
void NGL_ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *pixels);
void NGL_DrawPixels(GLsizei w, GLsizei h, GLenum format, GLenum type, const void *pixels);
void NGL_Viewport(GLint x, GLint y, GLsizei w, GLsizei h);
void NGL_Clear(GLbitfield mask);
void NGL_Finish(void);

/* VR hooks: the engine projects on the CPU into a W x H "screen"; the VR layer renders
 * that screen into its own FBO (bound whenever the engine would draw to framebuffer 0). */
void NGL_SetTargetFramebuffer(GLuint fbo, int w, int h);
void NGL_BindTexture(GLenum target, GLuint tex);
const GLubyte *NGL_GetString(GLenum name);
#ifdef __cplusplus
}
#endif

#define glBegin(m)              NGL_Begin(m)
#define glEnd()                 NGL_End()
#define glVertex2i(x, y)        NGL_Vertex3f((GLfloat)(x), (GLfloat)(y), 0.f)
#define glVertex2f(x, y)        NGL_Vertex3f((GLfloat)(x), (GLfloat)(y), 0.f)
#define glVertex3f(x, y, z)     NGL_Vertex3f((GLfloat)(x), (GLfloat)(y), (GLfloat)(z))
#define glTexCoord2f(s, t)      NGL_TexCoord4f((GLfloat)(s), (GLfloat)(t), 0.f, 1.f)
#define glTexCoord4f(s, t, r, q) NGL_TexCoord4f((GLfloat)(s), (GLfloat)(t), (GLfloat)(r), (GLfloat)(q))
#define glColor4f(r, g, b, a)   NGL_Color4f(r, g, b, a)
#define glColor3f(r, g, b)      NGL_Color4f(r, g, b, 1.f)
#define glColor4ub(r, g, b, a)  NGL_Color4f((r) / 255.f, (g) / 255.f, (b) / 255.f, (a) / 255.f)
#define glColor3ub(r, g, b)     NGL_Color4f((r) / 255.f, (g) / 255.f, (b) / 255.f, 1.f)
#define glColor4ubv(v)          NGL_Color4f(((const GLubyte *)(v))[0] / 255.f, ((const GLubyte *)(v))[1] / 255.f, ((const GLubyte *)(v))[2] / 255.f, ((const GLubyte *)(v))[3] / 255.f)
#define glColor3ubv(v)          NGL_Color4f(((const GLubyte *)(v))[0] / 255.f, ((const GLubyte *)(v))[1] / 255.f, ((const GLubyte *)(v))[2] / 255.f, 1.f)
#define glMatrixMode(m)         NGL_MatrixMode(m)
#define glLoadIdentity()        NGL_LoadIdentity()
#define glOrtho(l, r, b, t, n, f) NGL_Ortho(l, r, b, t, n, f)
#define glEnable(c)             NGL_Enable(c)
#define glDisable(c)            NGL_Disable(c)
#define glTexEnvi(t, p, v)      NGL_TexEnvi(t, p, v)
#define glPushAttrib(m)         NGL_PushAttrib(m)
#define glPopAttrib()           NGL_PopAttrib()
#define glPolygonMode(f, m)     NGL_PolygonMode(f, m)
#define glShadeModel(m)         ((void)(m))
#define glHint(t, m)            ((void)(t), (void)(m))
#define glColorTable(a, b, c, d, e, f) ((void)0)
#define glTexParameteri(t, p, v) NGL_TexParameteri(t, p, v)
#define glTexImage2D(t, l, i, w, h, b, f, ty, p) NGL_TexImage2D(t, l, i, w, h, b, f, ty, p)
#define glTexSubImage2D(t, l, x, y, w, h, f, ty, p) NGL_TexSubImage2D(t, l, x, y, w, h, f, ty, p)
#define glGetIntegerv(p, v)     NGL_GetIntegerv(p, v)
#define glReadPixels(x, y, w, h, f, t, p) NGL_ReadPixels(x, y, w, h, f, t, p)
#define glDrawPixels(w, h, f, t, p) NGL_DrawPixels(w, h, f, t, p)
#define glViewport(x, y, w, h)  NGL_Viewport(x, y, w, h)
#define glClear(m)              NGL_Clear(m)
#define glFinish()              NGL_Finish()
#define glBindTexture(t, x)     NGL_BindTexture(t, x)
#define glGetString(n)          NGL_GetString(n)
