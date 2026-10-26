/* NoGravity SDL2 platform helpers (window, GL context, event pump extras, key map). */
#pragma once
#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

struct RLXSYSTEM;
struct _gx_display_mode_info;

void  NG_Startup(int argc, char **argv);
void *NG_ListModes(struct RLXSYSTEM *rlx);            /* returns GXDISPLAYMODEINFO*, heap-allocated */
void *NG_CreateWindow(int w, int h, int fullscreen, int msaa);
void  NG_DestroyWindow(void);
void  NG_SwapWindow(void);
SDL_Window *NG_GetWindow(void);

int   NG_HandleEvent(const SDL_Event *e);              /* 1 = consumed */
int   NG_MapKey(SDL_Keycode sym);                      /* engine s_keymap code or 0 */
void  NG_WarpMouse(int x, int y);
int   NG_TakeWheel(void);
void  NG_GetMouseState(int *x, int *y);

/* Hooks for the Quest layer (no-ops on desktop). */
extern void (*NG_PresentHook)(void);                   /* replaces SDL_GL_SwapWindow when set */

#ifdef __cplusplus
}
#endif
