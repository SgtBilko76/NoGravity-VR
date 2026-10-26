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
#ifdef __cplusplus
}
#endif
