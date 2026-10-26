/* NoGravity VR - stereo flight rendering (game side, C, built for the Quest only).
 *
 * Called from NG_GamePlay instead of the flat "render scene + overlay". The game's own frame
 * build (V3XScene_Viewport_Build at the end of the previous loop pass: collisions, animated
 * materials, centre camera) stays untouched. Here, for each eye:
 *   - the cockpit camera gets the head pose (orientation + position, metres -> world units),
 *   - V3XScene_Viewport_BuildEye re-runs only the geometry stage (transform, cull, project) into a
 *     square eye viewport whose focal length matches the eye's field of view,
 *   - the scene, the per-eye starfield, explosions and lens flares are drawn into the eye's
 *     OpenXR swapchain image.
 * Afterwards the centre camera/viewport and every object's state are restored (game logic and
 * the HUD read them), and the HUD-only overlay is drawn into the transparent panel layer that
 * ng_quest.cpp places at the game's own field of view (crosshair on the ship's nose).
 *
 * Engine view space = OpenXR convention (x right, y up, looking down -z); view = M * (world - pos),
 * rows of M = camera axes in world space.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
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

void V3XScene_Viewport_BuildEye(V3XSCENE *Scene);

int g_ngqEye = -1;

static u_int32_t *s_savedState;
static V3XSCALAR *s_savedDist;
static int s_savedCap;
static float s_unitsPerMeter;

/* explosions are projected to 2D in NG_FXUpdate with the centre camera: redo it for this eye */
static void ProjectEffects(void)
{
    SGEffect *p = g_SGGame.pExpl;
    int32_t i;
    for (i = g_SGGame.numEffects; i != 0; p++, i--)
    {
        if (p->Dispo || !p->pos)
            continue;
        if (V3XVector_TransformProject_pts(p->pos, &p->pos2D[0]))
        {
            V3XSCALAR sx = V3X.ViewPort.Focal / p->pos2D[0].z;
            p->size2D[0].y = p->Size * sx;
            p->size2D[0].x = p->size2D[0].y * V3X.ViewPort.Ratio;
            p->pos2D[0].z = -p->pos2D[0].z;
            p->fce.visible = 1;
        }
        else
            p->fce.visible = 0;
        p->pos2D[1] = p->pos2D[0];
        p->size2D[1] = p->size2D[0];
    }
}

static float UnitsPerMeter(void)
{
    /* world units are not metres: scale head motion/IPD by the player's ship
       (bounding radius ~ 6 m for the fighters) unless NGQ_UNITS_PER_METER is set */
    if (s_unitsPerMeter <= 0.f)
    {
        const char *e = getenv("NGQ_UNITS_PER_METER");
        float r = 0.f;
        if (g_pPlayer && g_pPlayer->J.OVI && g_pPlayer->J.OVI->ORI)
            r = g_pPlayer->J.OVI->ORI->global_rayon;
        s_unitsPerMeter = e ? (float)atof(e) : (r > 0.f ? r / 6.f : 1.f);
        if (s_unitsPerMeter <= 0.f)
            s_unitsPerMeter = 1.f;
        printf("[VR] ship radius %.2f units -> %.3f units per metre\n", r, s_unitsPerMeter); fflush(stdout);
    }
    return s_unitsPerMeter;
}

int NGVR_RenderStereo(V3XSCENE *Scene)
{
    V3XCAMERA cam;
    GXVIEWPORT view;
    V3XVIEWPORT vp;
    float Mc[9], pos[3];
    int eye, i;

    if (!NGQ_StereoBegin())
        return 0;

    cam = V3X.Camera;
    view = GX.View;
    vp = V3X.ViewPort;
    memcpy(Mc, V3X.Camera.M.Matrix, sizeof Mc);
    pos[0] = V3X.Camera.M.v.Pos.x; pos[1] = V3X.Camera.M.v.Pos.y; pos[2] = V3X.Camera.M.v.Pos.z;

    if (s_savedCap < Scene->numOVI)
    {
        s_savedCap = Scene->numOVI + 64;
        s_savedState = (u_int32_t *)realloc(s_savedState, s_savedCap * sizeof(u_int32_t));
        s_savedDist = (V3XSCALAR *)realloc(s_savedDist, s_savedCap * sizeof(V3XSCALAR));
    }
    for (i = 0; i < Scene->numOVI; i++)
    {
        s_savedState[i] = Scene->OVI[i].state;
        s_savedDist[i] = Scene->OVI[i].distance;
    }

    /* keep the centre camera matrix (chase views compute it from keys in MatrixBuild) */
    V3X.Camera.matrix_Method = V3XMATRIX_Custom;

    for (eye = 0; eye < 2; eye++)
    {
        float q[4], p[3], tanHalf, R[9], M[9], upm = UnitsPerMeter();
        int w, h, r, c, k;
        if (!NGQ_StereoEyeBegin(eye, q, p, &tanHalf, &w, &h))
            continue;
        /* R: eye -> ship frame (row-major), from the quaternion */
        {
            float x = q[0], y = q[1], z = q[2], ww = q[3];
            R[0] = 1 - 2 * (y * y + z * z); R[1] = 2 * (x * y - z * ww);     R[2] = 2 * (x * z + y * ww);
            R[3] = 2 * (x * y + z * ww);     R[4] = 1 - 2 * (x * x + z * z); R[5] = 2 * (y * z - x * ww);
            R[6] = 2 * (x * z - y * ww);     R[7] = 2 * (y * z + x * ww);     R[8] = 1 - 2 * (x * x + y * y);
        }
        /* world -> eye = R^T * (world -> ship view) */
        for (r = 0; r < 3; r++)
            for (c = 0; c < 3; c++)
            {
                float s = 0.f;
                for (k = 0; k < 3; k++)
                    s += R[k * 3 + r] * Mc[k * 3 + c];
                M[r * 3 + c] = s;
            }
        memcpy(V3X.Camera.M.Matrix, M, sizeof M);
        /* eye position: ship-frame offset -> world (M^T maps view to world) */
        V3X.Camera.M.v.Pos.x = pos[0] + upm * (Mc[0] * p[0] + Mc[3] * p[1] + Mc[6] * p[2]);
        V3X.Camera.M.v.Pos.y = pos[1] + upm * (Mc[1] * p[0] + Mc[4] * p[1] + Mc[7] * p[2]);
        V3X.Camera.M.v.Pos.z = pos[2] + upm * (Mc[2] * p[0] + Mc[5] * p[1] + Mc[8] * p[2]);

        GX.View.xmin = 0; GX.View.ymin = 0;
        GX.View.xmax = w - 1; GX.View.ymax = h - 1;
        GX.View.lWidth = w; GX.View.lHeight = h;
        V3X.ViewPort.center.x = (float)(w / 2);
        V3X.ViewPort.center.y = (float)(h / 2);
        V3X.ViewPort.Focal = -((float)h * 0.5f) / tanHalf; /* same sign as V3XViewport_Setup */
        V3X.ViewPort.Ratio = 1.f;

        g_ngqEye = eye;
        V3XScene_Viewport_BuildEye(Scene);
        V3XScene_Viewport_Clear(Scene);
        V3XScene_Viewport_Render(Scene);
        ProjectEffects();
        NG_FXDraw();
        if (g_SGSettings.LensFX)
            NG_FXFlare();
        g_ngqEye = -1;
        NGQ_StereoEyeEnd(eye);
    }

    /* back to the centre camera: rebuild its geometry (game logic and HUD read the view-space
       matrices/projections), then restore the exact object states of the real frame build */
    V3X.Camera = cam;
    GX.View = view;
    V3X.ViewPort = vp;
    V3X.Camera.matrix_Method = V3XMATRIX_Custom;
    V3XScene_Viewport_BuildEye(Scene);
    V3X.Camera.matrix_Method = cam.matrix_Method;
    V3X.ViewPort = vp;
    for (i = 0; i < Scene->numOVI; i++)
    {
        Scene->OVI[i].state = s_savedState[i];
        Scene->OVI[i].distance = s_savedDist[i];
    }
    ProjectEffects();

    /* HUD layer: the panel (whole game frame, s_w x s_h pixels) is placed so that each pixel
       sits in the direction the flat game projects it to: pixel (px,py) <-> view direction
       ((px - cx) / (|F| * Ratio), (cy - py) / |F|). The 3D viewport (GX.View) can be smaller than
       the frame and its centre (= the ship's nose, where the crosshair is drawn) need not be the
       frame centre, so the quad is offset accordingly. */
    {
        float fy = fabsf(vp.Focal), rx = vp.Ratio > 0.f ? vp.Ratio : 1.f;
        float W = (float)GX.View.lWidth, H = (float)GX.View.lHeight;
        static int s_logged;
        if (fy > 1.f)
        {
            float tl = (0.f - vp.center.x) / (fy * rx), tr = (W - vp.center.x) / (fy * rx);
            float tu = (vp.center.y - 0.f) / fy, td = (vp.center.y - H) / fy;
            if (s_logged++ % 600 == 0)
                printf("[VR] view %d,%d-%d,%d frame %dx%d centre %.1f,%.1f focal %.1f ratio %.3f -> HUD tan L %.3f R %.3f U %.3f D %.3f\n",
                       view.xmin, view.ymin, view.xmax, view.ymax, (int)W, (int)H, vp.center.x, vp.center.y, vp.Focal, vp.Ratio, tl, tr, tu, td), fflush(stdout);
            NGQ_StereoHudBegin(tl, tr, tu, td);
        }
        else
            NGQ_StereoHudBegin(-0.8f, 0.8f, 0.6f, -0.6f);
    }
    return 1;
}
