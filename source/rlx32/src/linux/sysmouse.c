//-------------------------------------------------------------------------
/*
Copyright (C) 1996, 2004 - realtech VR

This file is part of No Gravity 1.9

No Gravity is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

Original Source: 1996 - Stephane Denis
Prepared for public release: 02/24/2004 - Stephane Denis, realtech VR
Linux/SDL Port: 2005 - Matt Williams
*/
//-------------------------------------------------------------------------

#include <SDL/SDL.h>

#include "_rlx32.h"
#include "_rlx.h"
#include "sysctrl.h"
#ifdef NG_SDL2
#include "ng_sdl2.h"
#endif

static int MouseOpen(void *hnd)
{
  // SDL only supports a single mouse device with 3 buttons and 3 axes.
  sMOU->device = NULL;
  sMOU->numControllers = 1;
  sMOU->numButtons = 3;
  sMOU->numAxes = 3;

  // Ensure that joystick events are processed automatically by SDL.
  SDL_JoystickEventState(SDL_ENABLE);

  return TRUE;
}

static void MouseRelease()
{
  // Nothing to do.
}

static void MouseShow()
{
  // Just call through into SDL.
  SDL_ShowCursor(1);
}

static void MouseHide()
{
  // Just call through into SDL.
  SDL_ShowCursor(0);
}

static void MouseSetPosition(u_int32_t x, u_int32_t y)
{
  // Just call through into SDL.
#ifdef NG_SDL2
  NG_WarpMouse((int)x, (int)y);
#else
  SDL_WarpMouse((Uint16)x, (Uint16)y);
#endif
}

static unsigned long MouseUpdate(void *dev)
{
  Uint8 buttons;

  // Copy the current button state to be the old button state.
  memcpy(sMOU->steButtons, sMOU->rgbButtons, sMOU->numButtons);

  // Get the button state and mouse movement.
#ifdef NG_SDL2
  {
    int rx, ry;
    buttons = (Uint8)SDL_GetRelativeMouseState(&rx, &ry);
    sMOU->lX = rx; sMOU->lY = ry;
  }
#else
  buttons = SDL_GetRelativeMouseState(&sMOU->lX, &sMOU->lY);
#endif

  // For each button, record its new state.
  sMOU->rgbButtons[0] = (u_int8_t)((buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) ? 1 : 0);
  sMOU->rgbButtons[1] = (u_int8_t)((buttons & SDL_BUTTON(SDL_BUTTON_MIDDLE)) ? 1 : 0);
  sMOU->rgbButtons[2] = (u_int8_t)((buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) ? 1 : 0);

  // SDL treats the Z axis (mouse wheel) as buttons, rather than an axis.
#ifdef NG_SDL2
  sMOU->lZ = NG_TakeWheel();
  {
    int ax, ay;
    NG_GetMouseState(&ax, &ay);
    sMOU->x = ax; sMOU->y = ay;
  }
#else
  sMOU->lZ = ((buttons & SDL_BUTTON(SDL_BUTTON_WHEELUP)) ? 1 : 0) -
             ((buttons & SDL_BUTTON(SDL_BUTTON_WHEELDOWN)) ? 1 : 0);

  // Get the absolute mouse position.
  (void)SDL_GetMouseState(&sMOU->x, &sMOU->y);
#endif

  return TRUE;
}

_RLXEXPORTFUNC MSE_ClientDriver *MSE_SystemGetInterface_STD(void)
{
  static MSE_ClientDriver driver =
  {
    MouseOpen,
    MouseRelease,
    MouseShow,
    MouseHide,
    MouseSetPosition,
    MouseUpdate
  };

  // Set the driver.
  sMOU = &driver;

  return sMOU;
}

