/* Desktop stand-in for port/quest/src/ng_quest.cpp (CMake -DNG_FAKE_VR=ON).
 * Lets the Quest-only game code (NG_QUEST: stereo flight in ng_vr_game.c, forced settings)
 * run in a normal SDL window: both eyes are shown side by side (top), the HUD panel below.
 * Fake head: env NGQ_FAKE_YAW / NGQ_FAKE_PITCH (degrees, + = left / up), IPD 64 mm.
 */
#include "ng_quest.h"
#include <SDL.h>
#include <GLES3/gl3.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>

extern "C" void NGL_SetTargetFramebuffer(GLuint fbo, int w, int h);
extern "C" void NGL_PushScreen(int w, int h);
extern "C" void NGL_PopScreen(void);

namespace {
int s_w = 1024, s_h = 768, s_eye = 512;
GLuint s_panelFbo, s_panelTex, s_eyeFbo[2], s_eyeTex[2], s_eyeDepth[2];
bool s_stereo = false, s_eyesDone = false;
float s_hudX = 0.8f, s_hudY = 0.6f;
const float kTan = 1.2f;
int s_frames = 0;

GLuint makeTarget(int w, int h, GLuint *tex, GLuint *depth) {
    GLuint fb;
    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
    glGenRenderbuffers(1, depth);
    glBindRenderbuffer(GL_RENDERBUFFER, *depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, *depth);
    return fb;
}
void bindPanel() { NGL_SetTargetFramebuffer(s_panelFbo, s_w, s_h); glViewport(0, 0, s_w, s_h); }
} // namespace

extern "C" {
void NGQ_Boot(void) {}
void NGQ_CreateVirtualJoystick(void) {}
int NGQ_IsVirtualJoystick(int) { return 1; }
void NGQ_FlightTick(void) {}

void NGQ_InitGL(int w, int h) {
    s_w = w; s_h = h;
    GLuint d;
    s_panelFbo = makeTarget(w, h, &s_panelTex, &d);
    for (int e = 0; e < 2; e++) s_eyeFbo[e] = makeTarget(s_eye, s_eye, &s_eyeTex[e], &s_eyeDepth[e]);
    bindPanel();
    printf("[FAKEVR] init %dx%d eyes %d\n", w, h, s_eye);
}

int NGQ_StereoBegin(void) {
    s_stereo = !getenv("NGQ_FLAT");
    s_eyesDone = false;
    return s_stereo;
}

int NGQ_StereoEyeBegin(int eye, float q[4], float p[3], float *tanHalf, int *w, int *h) {
    const float yaw = (getenv("NGQ_FAKE_YAW") ? (float)atof(getenv("NGQ_FAKE_YAW")) : 0.f) * 3.14159265f / 180.f;
    const float pitch = (getenv("NGQ_FAKE_PITCH") ? (float)atof(getenv("NGQ_FAKE_PITCH")) : 0.f) * 3.14159265f / 180.f;
    /* q = yaw (about +y) * pitch (about +x) */
    const float cy = cosf(yaw * .5f), sy = sinf(yaw * .5f), cp = cosf(pitch * .5f), sp = sinf(pitch * .5f);
    q[0] = cy * sp; q[1] = sy * cp; q[2] = -sy * sp; q[3] = cy * cp;
    const float off = eye == 0 ? -0.032f : 0.032f;
    p[0] = off * cosf(yaw); p[1] = 0.f; p[2] = -off * sinf(yaw);
    *tanHalf = kTan;
    *w = *h = s_eye;
    NGL_SetTargetFramebuffer(s_eyeFbo[eye], s_eye, s_eye);
    NGL_PushScreen(s_eye, s_eye);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return 1;
}

void NGQ_StereoEyeEnd(int eye) {
    NGL_PopScreen();
    if (eye == 1) s_eyesDone = true;
}

void NGQ_StereoHudBegin(float tanX, float tanY) {
    s_hudX = tanX; s_hudY = tanY;
    bindPanel();
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void NGQ_Present(void) {
    SDL_Window *win = SDL_GL_GetCurrentWindow();
    int ww = s_w, wh = s_h;
    if (win) SDL_GL_GetDrawableSize(win, &ww, &wh);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glViewport(0, 0, ww, wh);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.15f, 0.15f, 0.15f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (s_stereo && s_eyesDone) {
        /* eyes side by side in the top half (GL origin bottom-left), HUD panel bottom centre */
        const int ew = ww / 2, eh = wh / 2 + wh / 6;
        const int sz = ew < eh ? ew : eh;
        for (int e = 0; e < 2; e++) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, s_eyeFbo[e]);
            glBlitFramebuffer(0, 0, s_eye, s_eye, e * ew, wh - sz, e * ew + sz, wh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, s_panelFbo);
        const int pw = (wh - sz) * s_w / s_h;
        glBlitFramebuffer(0, 0, s_w, s_h, (ww - pw) / 2, 0, (ww - pw) / 2 + pw, wh - sz, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        if ((s_frames++ % 300) == 0) printf("[FAKEVR] stereo frame %d, HUD tan %.3f x %.3f\n", s_frames, s_hudX, s_hudY);
    } else {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, s_panelFbo);
        glBlitFramebuffer(0, 0, s_w, s_h, 0, 0, ww, wh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }
    if (win) SDL_GL_SwapWindow(win);
    s_stereo = s_eyesDone = false;
    bindPanel();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
}
