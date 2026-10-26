/* NoGravity VR - Quest host (Android/OpenXR side of the SDL2 port). */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
/* Called first thing in main(): logcat mirror, crash handler, cwd, bundled data. */
void NGQ_Boot(void);
/* After SDL_Init: Touch controllers as an SDL virtual joystick. */
void NGQ_CreateVirtualJoystick(void);
int NGQ_IsVirtualJoystick(int index);
/* Called once the SDL GL ES context is current; w/h = the game's video mode. */
void NGQ_InitGL(int w, int h);
/* Replaces SDL_GL_SwapWindow: hands the finished game frame to OpenXR. */
void NGQ_Present(void);
/* Stereo flight frame (port/quest/src/ng_vr_game.c):
   StereoBegin waits for the XR frame and locates the eyes (0 = render flat this frame);
   per eye: EyeBegin binds its swapchain image (q = orientation xyzw, p = position in metres,
   both in the recentred ship frame; symmetric half-angle tangent; size) ... EyeEnd;
   HudBegin binds the transparent HUD panel (game field of view as tangents). NGQ_Present submits. */
int NGQ_StereoBegin(void);
int NGQ_StereoEyeBegin(int eye, float q[4], float p[3], float *tanHalf, int *w, int *h);
void NGQ_StereoEyeEnd(int eye);
void NGQ_StereoHudBegin(float tanX, float tanY);
#ifdef __cplusplus
}
#endif
