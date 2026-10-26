/* NoGravity SDL2 platform helpers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ng_sdl2.h"
#ifdef NG_QUEST
#include "ng_quest.h"
#endif

#include "_rlx32.h"
#include "_rlx.h"
#include "systools.h"
#include "gx_struc.h"
#include "sysctrl.h"

void (*NG_PresentHook)(void) = NULL;

static SDL_Window   *g_win = NULL;
static SDL_GLContext g_ctx = NULL;
static int g_wheel = 0;
static int g_w = 640, g_h = 480;

void NG_Startup(int argc, char **argv)
{
    (void)argc; (void)argv;
#ifdef NG_QUEST
    NGQ_Boot();
#endif
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS | SDL_INIT_JOYSTICK) != 0)
        fprintf(stderr, "[NG] SDL_Init failed: %s\n", SDL_GetError());
#ifdef NG_QUEST
    NGQ_CreateVirtualJoystick();
#endif
    fprintf(stderr, "[NG] SDL %d.%d.%d started\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
}

void *NG_ListModes(struct RLXSYSTEM *rlx)
{
#ifdef NG_QUEST
    /* one fixed 4:3 mode: the frame becomes the VR virtual screen */
    static const int sizes[][2] = { {1024, 768} };
#else
    static const int sizes[][2] = { {640, 480}, {800, 600}, {1024, 768}, {1280, 960}, {1600, 1200} };
#endif
    static const int bpps[] = { 16, 32 };
    const int n = (int)(sizeof(sizes) / sizeof(sizes[0])) * 2;
    const char *one = SDL_getenv("NG_MODE"); /* e.g. 1024x768: offer only this mode (test the Quest mode on PC) */
    if (one) {
        GXDISPLAYMODEINFO *d1 = (GXDISPLAYMODEINFO *)rlx->mm_heap->malloc(3 * sizeof(GXDISPLAYMODEINFO));
        int w = 1024, h = 768;
        sscanf(one, "%dx%d", &w, &h);
        d1[0].lWidth = w; d1[0].lHeight = h; d1[0].BitsPerPixel = 16; d1[0].mode = 0;
        d1[1].lWidth = w; d1[1].lHeight = h; d1[1].BitsPerPixel = 32; d1[1].mode = 1;
        d1[2].lWidth = d1[2].lHeight = d1[2].BitsPerPixel = 0; d1[2].mode = 0;
        return d1;
    }
    GXDISPLAYMODEINFO *d = (GXDISPLAYMODEINFO *)rlx->mm_heap->malloc((n + 1) * sizeof(GXDISPLAYMODEINFO));
    int i = 0, b, s;
    for (b = 0; b < 2; b++)
        for (s = 0; s < (int)(sizeof(sizes) / sizeof(sizes[0])); s++, i++)
        {
            d[i].lWidth = sizes[s][0];
            d[i].lHeight = sizes[s][1];
            d[i].BitsPerPixel = bpps[b];
            d[i].mode = i;
        }
    d[i].lWidth = d[i].lHeight = d[i].BitsPerPixel = 0;
    d[i].mode = 0;
    return d;
}

void *NG_CreateWindow(int w, int h, int fullscreen, int msaa)
{
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
#ifdef NG_QUEST
    fullscreen = 1;
#endif
    const char *forceWin = SDL_getenv("NG_WINDOWED");
    if (forceWin && *forceWin == '1')
        fullscreen = 0;
#ifdef NG_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, msaa ? 1 : 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, msaa);
    if (fullscreen)
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    if (g_win == NULL)
    {
        g_win = SDL_CreateWindow("No Gravity", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
        if (!g_win && msaa)
        {
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
            g_win = SDL_CreateWindow("No Gravity", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
        }
        if (!g_win)
        {
            fprintf(stderr, "[NG] SDL_CreateWindow failed: %s\n", SDL_GetError());
            return NULL;
        }
        g_ctx = SDL_GL_CreateContext(g_win);
        if (!g_ctx)
        {
            fprintf(stderr, "[NG] SDL_GL_CreateContext failed: %s\n", SDL_GetError());
            return NULL;
        }
        SDL_GL_SetSwapInterval(1);
#ifdef NG_QUEST
        NGQ_InitGL(w, h);
#endif
    }
    else
    {
        /* Mode change: keep the GL context (textures survive), just resize. */
        SDL_SetWindowSize(g_win, w, h);
    }
    SDL_GL_MakeCurrent(g_win, g_ctx);
    g_w = w; g_h = h;
    fprintf(stderr, "[NG] video mode %dx%d fullscreen=%d\n", w, h, fullscreen);
    return g_win;
}

void NG_DestroyWindow(void)
{
    /* Keep the window/context alive across mode switches: the engine calls
       ReleaseSurfaces then CreateSurface and expects its GL objects to be recreated
       anyway, but SDL/Android cannot recreate the native window. */
}

SDL_Window *NG_GetWindow(void) { return g_win; }

void NG_SwapWindow(void)
{
#ifdef NG_QUEST
    NGQ_Present();
    return;
#endif
    if (NG_PresentHook)
    {
        NG_PresentHook();
        return;
    }
    SDL_GL_SwapWindow(g_win);
}

int NG_HandleEvent(const SDL_Event *e)
{
    switch (e->type)
    {
    case SDL_MOUSEWHEEL:
        g_wheel += (e->wheel.y > 0) ? 1 : (e->wheel.y < 0 ? -1 : 0);
        return 1;
    case SDL_QUIT:
        fprintf(stderr, "[NG] quit requested\n");
        exit(0);
    default:
        return 0;
    }
}

int NG_TakeWheel(void)
{
    int w = g_wheel;
    g_wheel = 0;
    return w;
}

void NG_WarpMouse(int x, int y)
{
    if (g_win)
        SDL_WarpMouseInWindow(g_win, x, y);
}

void NG_GetMouseState(int *x, int *y)
{
    int ww = g_w, wh = g_h;
    SDL_GetMouseState(x, y);
    if (g_win)
        SDL_GetWindowSize(g_win, &ww, &wh);
    /* Fullscreen-desktop windows are larger than the game mode: scale to game pixels. */
    if (ww > 0 && wh > 0 && (ww != g_w || wh != g_h))
    {
        *x = *x * g_w / ww;
        *y = *y * g_h / wh;
    }
}

int NG_MapKey(SDL_Keycode k)
{
    if (k >= 'a' && k <= 'z')
    {
        static const int letters[26] = { s_a, s_b, s_c, s_d, s_e, s_f, s_g, s_h, s_i, s_j, s_k, s_l, s_m,
                                         s_n, s_o, s_p, s_q, s_r, s_s, s_t, s_u, s_v, s_w, s_x, s_y, s_z };
        return letters[k - 'a'];
    }
    if (k >= '1' && k <= '9')
        return s_1 + (k - '1');
    switch (k)
    {
    case SDLK_0: return s_0;
    case SDLK_BACKSPACE: return s_backspace;
    case SDLK_TAB: return s_tab;
    case SDLK_RETURN: return s_return;
    case SDLK_ESCAPE: return s_esc;
    case SDLK_SPACE: return s_space;
    case SDLK_QUOTE: return s_quote;
    case SDLK_COMMA: return s_coma;
    case SDLK_MINUS: return s_minus;
    case SDLK_PERIOD: return s_period;
    case SDLK_SLASH: return s_slash;
    case SDLK_SEMICOLON: return s_semicolon;
    case SDLK_EQUALS: return s_equals;
    case SDLK_LEFTBRACKET: return s_opensquare;
    case SDLK_RIGHTBRACKET: return s_closesquare;
    case SDLK_BACKSLASH: return s_backslash;
    case SDLK_BACKQUOTE: return s_tilda;
    case SDLK_DELETE: return s_delete;
    case SDLK_KP_0: return s_numinsert;
    case SDLK_KP_1: return s_numend;
    case SDLK_KP_2: return s_numdown;
    case SDLK_KP_3: return s_numpagedown;
    case SDLK_KP_4: return s_numleft;
    case SDLK_KP_5: return s_num5;
    case SDLK_KP_6: return s_numright;
    case SDLK_KP_7: return s_numhome;
    case SDLK_KP_8: return s_numup;
    case SDLK_KP_9: return s_numpageup;
    case SDLK_KP_PERIOD: return s_numdelete;
    case SDLK_KP_MINUS: return s_numminus;
    case SDLK_KP_PLUS: return s_numplus;
    case SDLK_KP_ENTER: return s_return;
    case SDLK_UP: return s_up;
    case SDLK_DOWN: return s_down;
    case SDLK_RIGHT: return s_right;
    case SDLK_LEFT: return s_left;
    case SDLK_INSERT: return s_insert;
    case SDLK_HOME: return s_home;
    case SDLK_END: return s_end;
    case SDLK_PAGEUP: return s_pageup;
    case SDLK_PAGEDOWN: return s_pagedown;
    case SDLK_F1: return s_f1;
    case SDLK_F2: return s_f2;
    case SDLK_F3: return s_f3;
    case SDLK_F4: return s_f4;
    case SDLK_F5: return s_f5;
    case SDLK_F6: return s_f6;
    case SDLK_F7: return s_f7;
    case SDLK_F8: return s_f8;
    case SDLK_F9: return s_f9;
    case SDLK_F10: return s_f10;
    case SDLK_F11: return s_f11;
    case SDLK_F12: return s_f12;
    case SDLK_NUMLOCKCLEAR: return s_numlock;
    case SDLK_CAPSLOCK: return s_capslock;
    case SDLK_SCROLLLOCK: return s_scrolllock;
    case SDLK_RSHIFT: return s_rightshift;
    case SDLK_LSHIFT: return s_leftshift;
    case SDLK_LCTRL: case SDLK_RCTRL: return s_ctrl;
    case SDLK_LALT: case SDLK_RALT: return s_alt;
    case SDLK_LGUI: return s_winleft;
    case SDLK_RGUI: return s_winright;
    case SDLK_PRINTSCREEN: return s_printscreen;
    case SDLK_APPLICATION: return s_winapp;
    default: return 0;
    }
}
