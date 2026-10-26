/* NoGravity VR - on-screen keyboard for the game's text entry (pilot name, RW_InputText).
 *
 * RW_InputText calls NGQ_VKeyboardStep() every loop pass (it edits the text directly) and the
 * menu renderer calls NGQ_VKeyboardDraw() before the frame is flipped. While it is open the VR
 * host stops turning A/B/sticks into Enter/Escape/arrow keys (NGQ_SetTextInput), so the
 * controllers drive only the keyboard:
 *   stick (either) = move the highlight, A or a trigger = press key,
 *   B = delete last letter, X = space, Y = OK, menu button = cancel.
 * A real (Bluetooth) keyboard keeps working through the original code path.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "_rlx32.h"
#include "_stub.h"
#include "_rlx.h"
#include "systools.h"
#include "sysresmx.h"
#include "systime.h"
#include "sysnetw.h"
#include "sysctrl.h"
#include "gx_struc.h"
#include "gx_csp.h"
#include "gx_flc.h"
#include "gx_cdc.h"
#include "gx_tools.h"
#include "v3xdefs.h"
#include "v3xtrig.h"
#include "v3x_1.h"
#include "v3x_2.h"
#include "v3xcoll.h"
#include "v3xrend.h"
#include "v3xmaps.h"
#include "v3xscene.h"
#include "iss_defs.h"
#include "gui_os.h"
#include "gx_rgb.h"
#include "lt_struc.h"
#include "lt_data.h"
#include "lt_func.h"
#include "ng_quest.h"

u_int32_t NG_PixelFormat(int r, int g, int b, int a); /* lt_menu.c */

enum { K_CHAR = 0, K_CASE, K_SPACE, K_DEL, K_OK, K_CANCEL };

typedef struct { const char *label; int kind; char ch; int span; } VKey;

#define COLS 10
static const char *s_rows[4] = { "1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM._!" };
static const VKey s_bottom[] = {
    { "CAPS", K_CASE, 0, 2 }, { "SPACE", K_SPACE, ' ', 3 }, { "DEL", K_DEL, 0, 2 },
    { "OK", K_OK, 0, 2 }, { "X", K_CANCEL, 0, 1 },
};
#define NBOTTOM ((int)(sizeof(s_bottom) / sizeof(s_bottom[0])))

static int s_active, s_row = 1, s_col = 0, s_lower = 0;
static u_int8_t s_prevBtn[16];
static int s_prevDir;
static u_int32_t s_repeatAt;

/* bottom row: column -> key index */
static int BottomIndex(int col)
{
    int i, c = 0;
    for (i = 0; i < NBOTTOM; i++)
    {
        if (col < c + s_bottom[i].span)
            return i;
        c += s_bottom[i].span;
    }
    return NBOTTOM - 1;
}
static int BottomStart(int idx)
{
    int i, c = 0;
    for (i = 0; i < idx; i++)
        c += s_bottom[i].span;
    return c;
}

void NGQ_VKeyboardBegin(void)
{
    s_active = 1;
    s_row = 1; s_col = 0;
    memset(s_prevBtn, 1, sizeof s_prevBtn); /* ignore buttons still held from the menu click */
    s_prevDir = -1;
    NGQ_SetTextInput(1);
}

void NGQ_VKeyboardEnd(void)
{
    s_active = 0;
    NGQ_SetTextInput(0);
}

static void Press(char *text, int numChar, u_int8_t *finish)
{
    size_t l = strlen(text);
    if (s_row < 4)
    {
        char c = s_rows[s_row][s_col];
        if (s_lower && c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (l < (size_t)(numChar - 2))
        {
            text[l] = c;
            text[l + 1] = 0;
        }
        return;
    }
    switch (s_bottom[BottomIndex(s_col)].kind)
    {
        case K_CASE: s_lower ^= 1; break;
        case K_SPACE:
            if (l < (size_t)(numChar - 2)) { text[l] = ' '; text[l + 1] = 0; }
            break;
        case K_DEL: if (l) text[l - 1] = 0; break;
        case K_OK: *finish = 1; break;
        case K_CANCEL: *finish = 2; break;
    }
}

void NGQ_VKeyboardStep(char *text, int numChar, u_int8_t *finish)
{
    int i, dir = 0, x, y, pressed[16];
    u_int32_t now = timer_ms();
    if (!s_active || !sJOY || !sJOY->numControllers)
        return;
    for (i = 0; i < 16; i++)
    {
        u_int8_t b = (i < sJOY->numButtons) ? (sJOY->rgbButtons[i] ? 1 : 0) : 0;
        pressed[i] = b && !s_prevBtn[i];
        s_prevBtn[i] = b;
    }
    /* left stick (lX/lY) or right stick X (lRx) / right stick Y (throttle lever axis is not raw):
       use the left stick plus right stick X */
    x = sJOY->lX - 32768;
    y = sJOY->lY - 32768;
    if (abs(sJOY->lRx - 32768) > abs(x))
        x = -(sJOY->lRx - 32768); /* lRx carries -rightX (roll) */
    if (abs(y) > 20000 && abs(y) >= abs(x)) dir = y < 0 ? 1 : 2;
    else if (abs(x) > 20000) dir = x < 0 ? 3 : 4;
    if (dir && (dir != s_prevDir || now >= s_repeatAt))
    {
        int bottom = s_row == 4;
        s_repeatAt = now + (dir != s_prevDir ? 400 : 140);
        switch (dir)
        {
            case 1: s_row = s_row > 0 ? s_row - 1 : 4; break;
            case 2: s_row = s_row < 4 ? s_row + 1 : 0; break;
            case 3:
                if (bottom) { int k = BottomIndex(s_col); k = k > 0 ? k - 1 : NBOTTOM - 1; s_col = BottomStart(k); }
                else s_col = s_col > 0 ? s_col - 1 : COLS - 1;
                break;
            case 4:
                if (bottom) { int k = BottomIndex(s_col); k = k < NBOTTOM - 1 ? k + 1 : 0; s_col = BottomStart(k); }
                else s_col = s_col < COLS - 1 ? s_col + 1 : 0;
                break;
        }
        if (s_row == 4 && !bottom)
            s_col = BottomStart(BottomIndex(s_col));
    }
    s_prevDir = dir;

    /* buttons (ng_quest.cpp): 0 A, 1 B, 2 RT, 3 LT, 4 X, 5 Y */
    if (pressed[0] || pressed[2] || pressed[3])
        Press(text, numChar, finish);
    if (pressed[1])
    {
        size_t l = strlen(text);
        if (l) text[l - 1] = 0;
    }
    if (pressed[4])
    {
        size_t l = strlen(text);
        if (l < (size_t)(numChar - 2)) { text[l] = ' '; text[l + 1] = 0; }
    }
    if (pressed[5])
        *finish = 1;
}

static void DrawKey(int x, int y, int w, int h, const char *label, int sel)
{
    GXSPRITEGROUP *font = g_pFontMenuSml ? g_pFontMenuSml : g_pFont;
    int tw = CSPG_TxLen(label, font), th = font->item[0].LY;
    GX.gi.drawFilledRect(x, y, x + w - 1, y + h - 1, sel ? NG_PixelFormat(60, 120, 230, 0) : NG_PixelFormat(28, 34, 52, 0));
    if (sel)
    {
        GX.gi.drawFilledRect(x, y, x + w - 1, y + 2, NG_PixelFormat(255, 255, 255, 0));
        GX.gi.drawFilledRect(x, y + h - 3, x + w - 1, y + h - 1, NG_PixelFormat(255, 255, 255, 0));
    }
    CSP_Color(NG_PixelFormat(235, 235, 245, 0));
    CSP_WriteText((char *)label, x + (w - tw) / 2, y + (h - th) / 2, font);
}

void NGQ_VKeyboardDraw(void)
{
    int kw, kh, gap, x0, y0, r, c, W, H;
    char lab[2] = { 0, 0 };
    if (!s_active)
        return;
    W = GX.View.xmax - GX.View.xmin + 1;
    H = GX.View.ymax - GX.View.ymin + 1;
    gap = W / 160;
    kw = (W * 70 / 100 - gap * (COLS - 1)) / COLS;
    kh = H / 14;
    x0 = GX.View.xmin + (W - (kw * COLS + gap * (COLS - 1))) / 2;
    y0 = GX.View.ymax - (kh + gap) * 5 - H / 30;
    GX.gi.drawFilledRect(x0 - gap * 2, y0 - gap * 2, x0 + kw * COLS + gap * (COLS + 1), y0 + (kh + gap) * 5 + gap, NG_PixelFormat(8, 10, 18, 0));
    for (r = 0; r < 4; r++)
        for (c = 0; c < COLS; c++)
        {
            char ch = s_rows[r][c];
            if (s_lower && ch >= 'A' && ch <= 'Z')
                ch = (char)(ch - 'A' + 'a');
            lab[0] = ch;
            DrawKey(x0 + c * (kw + gap), y0 + r * (kh + gap), kw, kh, lab, r == s_row && c == s_col);
        }
    for (c = 0; c < NBOTTOM; c++)
    {
        int s = BottomStart(c), sp = s_bottom[c].span;
        DrawKey(x0 + s * (kw + gap), y0 + 4 * (kh + gap), kw * sp + gap * (sp - 1), kh,
                s_bottom[c].label, s_row == 4 && BottomIndex(s_col) == c);
    }
    {
        const char *help = "Stick: move   A/Trigger: type   B: delete   X: space   Y: OK";
        GXSPRITEGROUP *f = g_pFont;
        CSP_Color(NG_PixelFormat(170, 180, 200, 0));
        CSP_WriteText((char *)help, GX.View.xmin + (W - CSPG_TxLen(help, f)) / 2, y0 + 5 * (kh + gap) + gap, f);
    }
}
