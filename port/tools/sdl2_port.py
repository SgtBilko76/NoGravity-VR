#!/usr/bin/env python3
"""Idempotent SDL 1.2 -> SDL2 edits of the NoGravity platform layer (guarded by NG_SDL2).

Run from anywhere: python3 port/tools/sdl2_port.py
Each edit asserts its anchor exists (or that it is already applied).
"""
import pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2] / "source"
changed = []


def edit(rel, old, new):
    p = ROOT / rel
    raw = p.read_bytes().decode("latin-1")
    crlf = "\r\n" in raw
    t = raw.replace("\r\n", "\n")
    if new in t and (old not in t or new not in old):
        return
    assert old in t, f"anchor not found in {rel}: {old[:70]!r}"
    assert t.count(old) == 1, f"anchor not unique in {rel}: {old[:70]!r}"
    t = t.replace(old, new)
    if crlf:
        t = t.replace("\n", "\r\n")
    p.write_bytes(t.encode("latin-1"))
    changed.append(rel)


def edit_all(rel, old, new):
    p = ROOT / rel
    raw = p.read_bytes().decode("latin-1")
    crlf = "\r\n" in raw
    t = raw.replace("\r\n", "\n")
    if old not in t:
        assert new in t, f"anchor not found in {rel}: {old[:70]!r}"
        return
    t = t.replace(old, new)
    if crlf:
        t = t.replace("\n", "\r\n")
    p.write_bytes(t.encode("latin-1"))
    changed.append(rel)


# ------------------------------------------------------------------ video (OpenGL client)
GL = "rlx32/src/renderers/opengl/linux/sdl_gl.cpp"
edit(GL, "#include <SDL/SDL.h>\n#include <GL/gl.h>\n",
     "#include <SDL/SDL.h>\n#ifdef NG_GLES\n#include \"ng_gles.h\"\n#else\n#include <GL/gl.h>\n#endif\n")
edit(GL, "static SDL_Surface *g_pSurface = NULL;\n",
     "#ifdef NG_SDL2\n#include \"ng_sdl2.h\"\nstatic void *g_pSurface = NULL;\n#else\nstatic SDL_Surface *g_pSurface = NULL;\n#endif\n")
edit(GL, "    glFinish();\n    SDL_GL_SwapBuffers();\n",
     "#ifdef NG_SDL2\n    NG_SwapWindow();\n#else\n    glFinish();\n    SDL_GL_SwapBuffers();\n#endif\n")
# Mode list: SDL2 has no SDL_ListModes. Offer fixed 4:3 modes (the game UI is 4:3).
edit(GL, "  if (g_pDisplays == NULL)\n  {\n    memset(&fmts, 0, sizeof(SDL_PixelFormat));\n",
     "#ifdef NG_SDL2\n  if (g_pDisplays == NULL)\n  {\n    g_pDisplays = (GXDISPLAYMODEINFO *)NG_ListModes(g_pRLX);\n  }\n  if (0)\n#else\n  if (g_pDisplays == NULL)\n#endif\n  {\n    memset(&fmts, 0, sizeof(SDL_PixelFormat));\n")
edit(GL, "  const SDL_VideoInfo *vid_info;\n  SDL_PixelFormat fmts[3];\n",
     "#ifndef NG_SDL2\n  const SDL_VideoInfo *vid_info;\n#endif\n  SDL_PixelFormat fmts[3];\n")
edit(GL, "      vid_info = SDL_GetVideoInfo();\n      memcpy(&fmts[0], vid_info->vfmt, sizeof(SDL_PixelFormat));\n",
     "#ifndef NG_SDL2\n      vid_info = SDL_GetVideoInfo();\n      memcpy(&fmts[0], vid_info->vfmt, sizeof(SDL_PixelFormat));\n#endif\n")
edit(GL, "      modes = SDL_ListModes(&fmts[fmt_idx], SDL_OPENGL | ((g_pRLX->Video.Config & RLXVIDEO_Windowed) ? 0 : SDL_FULLSCREEN));\n\n      if (modes == (SDL_Rect **)-1)\n      {\n\t\tmodes",
     "#ifdef NG_SDL2\n      modes = NULL;\n#else\n      modes = SDL_ListModes(&fmts[fmt_idx], SDL_OPENGL | ((g_pRLX->Video.Config & RLXVIDEO_Windowed) ? 0 : SDL_FULLSCREEN));\n#endif\n\n      if (modes == (SDL_Rect **)-1)\n      {\n\t\tmodes")
edit(GL, "      modes = SDL_ListModes(&fmts[fmt_idx], SDL_OPENGL | ((g_pRLX->Video.Config & RLXVIDEO_Windowed) ? 0 : SDL_FULLSCREEN));\n\n      if (modes == (SDL_Rect **)-1)\n      {\n\tmodes",
     "#ifdef NG_SDL2\n      modes = NULL;\n#else\n      modes = SDL_ListModes(&fmts[fmt_idx], SDL_OPENGL | ((g_pRLX->Video.Config & RLXVIDEO_Windowed) ? 0 : SDL_FULLSCREEN));\n#endif\n\n      if (modes == (SDL_Rect **)-1)\n      {\n\tmodes")
edit(GL, "\tint sdl_flags = SDL_OPENGL | ((g_pRLX->Video.Config & RLXVIDEO_Windowed) ? 0 : SDL_FULLSCREEN);\n\tint multisampling = FALSE;\n",
     "#ifdef NG_SDL2\n\tint multisampling = FALSE;\n\tSYS_ASSERT(g_pDisplays != NULL);\n\tSYS_ASSERT(g_Mode != -1);\n\tg_pSurface = NG_CreateWindow(g_pDisplays[g_Mode].lWidth, g_pDisplays[g_Mode].lHeight,\n\t\t!(g_pRLX->Video.Config & RLXVIDEO_Windowed),\n\t\t(g_pRLX->pGX->View.Flags & GX_CAPS_MULTISAMPLING) ? g_pRLX->pGX->View.Multisampling : 0);\n#else\n\tint sdl_flags = SDL_OPENGL | ((g_pRLX->Video.Config & RLXVIDEO_Windowed) ? 0 : SDL_FULLSCREEN);\n\tint multisampling = FALSE;\n")
edit(GL, "\t  g_pSurface = SDL_SetVideoMode(g_pDisplays[g_Mode].lWidth, g_pDisplays[g_Mode].lHeight, g_pDisplays[g_Mode].BitsPerPixel, sdl_flags);\n\t}\n\t\n\tif (g_pSurface == NULL)",
     "\t  g_pSurface = SDL_SetVideoMode(g_pDisplays[g_Mode].lWidth, g_pDisplays[g_Mode].lHeight, g_pDisplays[g_Mode].BitsPerPixel, sdl_flags);\n\t}\n#endif\n\t\n\tif (g_pSurface == NULL)")
edit(GL, "\tif (multisampling)\n\t{\n\t  glEnable(GL_MULTISAMPLE_ARB);\n\t}\n",
     "#ifndef NG_GLES\n\tif (multisampling)\n\t{\n\t  glEnable(GL_MULTISAMPLE_ARB);\n\t}\n#endif\n")
edit(GL, "  SDL_FreeSurface(g_pSurface);\n  g_pRLX->pGX->Surfaces.maxSurface = 0;\n",
     "#ifdef NG_SDL2\n  NG_DestroyWindow();\n#else\n  SDL_FreeSurface(g_pSurface);\n#endif\n  g_pRLX->pGX->Surfaces.maxSurface = 0;\n")

# ------------------------------------------------------------------ keyboard + event pump
KB = "rlx32/src/linux/syskeyb.c"
edit(KB, "#include \"sysctrl.h\"\n",
     "#include \"sysctrl.h\"\n#ifdef NG_SDL2\n#include \"ng_sdl2.h\"\n#endif\n")
edit(KB, "  while (SDL_PollEvent(&evt) != 0)\n  {\n",
     "  while (SDL_PollEvent(&evt) != 0)\n  {\n#ifdef NG_SDL2\n    if (NG_HandleEvent(&evt))\n      continue;\n    if ((evt.type == SDL_KEYDOWN) || (evt.type == SDL_KEYUP))\n    {\n      int sc = NG_MapKey(evt.key.keysym.sym);\n      if (sc == 0)\n        continue;\n      if (evt.type == SDL_KEYDOWN)\n      {\n        SKEY_SET_BIT(keys, sc, 1);\n        sKEY->scanCode = sc;\n        if ((evt.key.keysym.sym >= 32) && (evt.key.keysym.sym < 127))\n          sKEY->charCode = (char)evt.key.keysym.sym;\n      }\n      else\n      {\n        SKEY_SET_BIT(keys, sc, 0);\n        sKEY->scanCode = 0;\n        sKEY->charCode = 0;\n      }\n    }\n    continue;\n#endif\n")

# ------------------------------------------------------------------ mouse
MO = "rlx32/src/linux/sysmouse.c"
edit(MO, "#include \"sysctrl.h\"\n",
     "#include \"sysctrl.h\"\n#ifdef NG_SDL2\n#include \"ng_sdl2.h\"\n#endif\n")
edit(MO, "  SDL_WarpMouse((Uint16)x, (Uint16)y);\n",
     "#ifdef NG_SDL2\n  NG_WarpMouse((int)x, (int)y);\n#else\n  SDL_WarpMouse((Uint16)x, (Uint16)y);\n#endif\n")
edit(MO, "  buttons = SDL_GetRelativeMouseState(&sMOU->lX, &sMOU->lY);\n",
     "#ifdef NG_SDL2\n  {\n    int rx, ry;\n    buttons = (Uint8)SDL_GetRelativeMouseState(&rx, &ry);\n    sMOU->lX = rx; sMOU->lY = ry;\n  }\n#else\n  buttons = SDL_GetRelativeMouseState(&sMOU->lX, &sMOU->lY);\n#endif\n")
edit(MO, "  sMOU->lZ = ((buttons & SDL_BUTTON(SDL_BUTTON_WHEELUP)) ? 1 : 0) -\n             ((buttons & SDL_BUTTON(SDL_BUTTON_WHEELDOWN)) ? 1 : 0);\n\n  // Get the absolute mouse position.\n  (void)SDL_GetMouseState(&sMOU->x, &sMOU->y);\n",
     "#ifdef NG_SDL2\n  sMOU->lZ = NG_TakeWheel();\n  {\n    int ax, ay;\n    NG_GetMouseState(&ax, &ay);\n    sMOU->x = ax; sMOU->y = ay;\n  }\n#else\n  sMOU->lZ = ((buttons & SDL_BUTTON(SDL_BUTTON_WHEELUP)) ? 1 : 0) -\n             ((buttons & SDL_BUTTON(SDL_BUTTON_WHEELDOWN)) ? 1 : 0);\n\n  // Get the absolute mouse position.\n  (void)SDL_GetMouseState(&sMOU->x, &sMOU->y);\n#endif\n")

# ------------------------------------------------------------------ threads
TM = "rlx32/src/linux/systime.c"
edit(TM, "  pThread->hThread = (void *)SDL_CreateThread((int(*)(void *))pThread->pFunc, pThread->pArgument);\n",
     "#ifdef NG_SDL2\n  pThread->hThread = (void *)SDL_CreateThread((int(*)(void *))pThread->pFunc, \"rlx\", pThread->pArgument);\n#else\n  pThread->hThread = (void *)SDL_CreateThread((int(*)(void *))pThread->pFunc, pThread->pArgument);\n#endif\n")

# ------------------------------------------------------------------ sound
SN = "rlx32/src/linux/snd_sdlmixer.c"
edit(SN, "  SDL_AudioDriverName(V3XA.Client->s_DrvName, 64);\n",
     "#ifdef NG_SDL2\n  {\n    const char *drv = SDL_GetCurrentAudioDriver();\n    snprintf(V3XA.Client->s_DrvName, 64, \"%s\", drv ? drv : \"SDL2\");\n  }\n#else\n  SDL_AudioDriverName(V3XA.Client->s_DrvName, 64);\n#endif\n")

# ------------------------------------------------------------------ main
ST = "rlx32/src/linux/_stublinux.cpp"
edit(ST, "int main(int argc, char *argv[])\n{\n  // TODO: Set up current directory.\n",
     "#ifdef NG_SDL2\n#include \"ng_sdl2.h\"\n#endif\nint main(int argc, char *argv[])\n{\n#ifdef NG_SDL2\n  NG_Startup(argc, argv);\n#endif\n  // TODO: Set up current directory.\n")

PNG = "rlx32/src/gx_png.c"
edit(PNG, "#include <pngstruct.h>\n", "#ifndef NG_SDL2\n#include <pngstruct.h>\n#endif\n")
edit(PNG, "(SYS_FILEHANDLE)png_ptr->io_ptr);", "(SYS_FILEHANDLE)png_get_io_ptr(png_ptr));")
edit(PNG, "\tif (setjmp(png_ptr->jmpbuf))", "\tif (setjmp(png_jmpbuf(png_ptr)))")

# String copies in the engine overlap their own buffers (trimSpace etc.): UB, and
# Android's FORTIFY libc aborts on it. Make sysStrCpy overlap-safe.
edit("rlx32/include/systools.h", "#define sysStrCpy strcpy\n",
     "#ifdef NG_SDL2\nstatic __inline char *ng_strcpy_safe(char *d, const char *s) { size_t n = strlen(s) + 1; memmove(d, s, n); return d; }\n#define sysStrCpy ng_strcpy_safe\n#else\n#define sysStrCpy strcpy\n#endif\n")

# Stack-heap realloc copies into a block that can overlap the old one.
edit("rlx32/src/systools.c", "        v = MM_heap_reserve(size);\n        sysMemCpy(v, block, size);\n",
     "        v = MM_heap_reserve(size);\n        memmove(v, block, size);\n")

# ------------------------------------------------------------------ 64-bit: 32-bit on-disk structs
SC = "rlx32/src/v3xscen2.c"
edit(SC, "static void RLXAPI *v3x_read_alloc(int32_t sz, int32_t n, int32_t n2, SYS_FILEHANDLE in)\n{\n    u_int8_t *tmp;\n    int s;\n    if (n2<n) \n\t\tn2 = n;\n    s = sz * n2; \n    if (!s) \n\t\treturn NULL;\n\ttmp = (u_int8_t*)MM_heap.malloc(s); \n    s = FIO_gzip.fread(tmp, sz, n, in);\n\tSYS_ASSERT(n == s);\n    return tmp;\n}\n",
     "static void RLXAPI *v3x_read_alloc(int32_t sz, int32_t n, int32_t n2, SYS_FILEHANDLE in)\n{\n    u_int8_t *tmp;\n    int s;\n    if (n2<n) \n\t\tn2 = n;\n    s = sz * n2; \n    if (!s) \n\t\treturn NULL;\n\ttmp = (u_int8_t*)MM_heap.malloc(s); \n    s = FIO_gzip.fread(tmp, sz, n, in);\n\tSYS_ASSERT(n == s);\n    return tmp;\n}\n"
     "#ifdef NG_SDL2\n/* NoGravity 64-bit port: the data files hold 32-bit struct images (4-byte pointers). */\n#include \"ng_conv32.h\"\n"
     "static void RLXAPI *v3x_read_alloc32(const NG_LAYOUT32 *L, int32_t n, int32_t n2, SYS_FILEHANDLE in)\n{\n    u_int8_t *tmp, *out;\n    int s;\n    if (n2<n) n2 = n;\n    if (!(L->size64 * n2)) return NULL;\n    out = (u_int8_t*)MM_heap.malloc(L->size64 * n2);\n    sysMemZero(out, L->size64 * n2);\n    if (!n) return out;\n    tmp = (u_int8_t*)MM_std.malloc(L->size32 * n);\n    s = FIO_gzip.fread(tmp, L->size32, n, in);\n    SYS_ASSERT(n == s);\n    NG_Convert32(L, out, tmp, n, L->size32);\n    MM_std.free(tmp);\n    return out;\n}\n"
     "static void ng_fread32(void *dst, const NG_LAYOUT32 *L, SYS_FILEHANDLE in)\n{\n    u_int8_t tmp[1024];\n    SYS_ASSERT(L->size32 <= sizeof(tmp));\n    FIO_gzip.fread(tmp, 1, L->size32, in);\n    sysMemZero(dst, L->size64);\n    NG_Convert32(L, dst, tmp, 1, L->size32);\n}\n"
     "/* light/camera records are stored as a V3XNODE-sized (32-bit) block */\nstatic void RLXAPI *v3x_read_node32(const NG_LAYOUT32 *L, SYS_FILEHANDLE in)\n{\n    u_int8_t tmp[1024];\n    size_t sz = L->size64 > sizeof(V3XNODE) ? L->size64 : sizeof(V3XNODE);\n    u_int8_t *out = (u_int8_t*)MM_heap.malloc(sz);\n    sysMemZero(out, sz);\n    FIO_gzip.fread(tmp, 1, ng_l32_V3XNODE.size32, in);\n    NG_Convert32(L, out, tmp, 1, ng_l32_V3XNODE.size32);\n    return out;\n}\n"
     "#define V3X_READ_ALLOC(T, n, n2, in) v3x_read_alloc32(&ng_l32_##T, n, n2, in)\n#define V3X_FREAD1(dst, T, in) ng_fread32(dst, &ng_l32_##T, in)\n#define V3X_READ_NODE(T, in) v3x_read_node32(&ng_l32_##T, in)\n#define V3X_SIZE32(T) (ng_l32_##T.size32)\n"
     "#else\n#define V3X_READ_ALLOC(T, n, n2, in) v3x_read_alloc(sizeof(T), n, n2, in)\n#define V3X_FREAD1(dst, T, in) FIO_gzip.fread(dst, 1, sizeof(T), in)\n#define V3X_READ_NODE(T, in) v3x_read_alloc(sizeof(V3XNODE), 1, -1, in)\n#define V3X_SIZE32(T) sizeof(T)\n#endif\n")
for T in ("V3XMATERIAL", "V3XPOLY", "V3XCL_ITEM", "V3XCL", "V3XTWEEN", "V3XKEYEX", "V3XTRI", "V3XTVI", "V3XORI", "V3XOVI"):
    pass
edit(SC, "\tMat = (V3XMATERIAL*)v3x_read_alloc(sizeof(V3XMATERIAL), numMaterial, -1, in);", "\tMat = (V3XMATERIAL*)V3X_READ_ALLOC(V3XMATERIAL, numMaterial, -1, in);")
edit(SC, "    obj =(V3XMESH*)v3x_read_alloc(sizeof(V3XMESH), 1, -1, in);\n#ifdef __BIG_ENDIAN__\n    BSWAP32((u_int32_t*)&obj->matrix, 12);\n\tBSWAP32((u_int32_t*)&obj->Tk, 3+3+1);\n#endif\n\n\treturn (V3XNODE*)obj;",
     "    obj =(V3XMESH*)V3X_READ_ALLOC(V3XMESH, 1, -1, in);\n#ifdef __BIG_ENDIAN__\n    BSWAP32((u_int32_t*)&obj->matrix, 12);\n\tBSWAP32((u_int32_t*)&obj->Tk, 3+3+1);\n#endif\n\n\treturn (V3XNODE*)obj;")
edit(SC, "    obj =(V3XMESH*)v3x_read_alloc(sizeof(V3XMESH), 1, -1, in);\n#ifdef __BIG_ENDIAN__\n    BSWAP16(", "    obj =(V3XMESH*)V3X_READ_ALLOC(V3XMESH, 1, -1, in);\n#ifdef __BIG_ENDIAN__\n    BSWAP16(")
edit(SC, "        obj->face = (V3XPOLY*) v3x_read_alloc(sizeof(V3XPOLY) , obj->numFaces, -1, in);", "        obj->face = (V3XPOLY*) V3X_READ_ALLOC(V3XPOLY, obj->numFaces, -1, in);")
edit(SC, "    light = (V3XLIGHT*)v3x_read_alloc(sizeof(V3XNODE), 1, -1, in);", "    light = (V3XLIGHT*)V3X_READ_NODE(V3XLIGHT, in);")
edit(SC, "    camera = (V3XCAMERA*)v3x_read_alloc(sizeof(V3XNODE), 1, -1, in);", "    camera = (V3XCAMERA*)V3X_READ_NODE(V3XCAMERA, in);")
edit(SC, "    V3XCL *Cs = (V3XCL*) v3x_read_alloc(sizeof(V3XCL), 1, -1, in);", "    V3XCL *Cs = (V3XCL*) V3X_READ_ALLOC(V3XCL, 1, -1, in);")
edit(SC, "\tSYS_ASSERT(sizeof(V3XCL_ITEM) == 64);\n    Cs->item = (V3XCL_ITEM*)v3x_read_alloc(sizeof(V3XCL_ITEM), Cs->numItem, -1, in);",
     "\tSYS_ASSERT(V3X_SIZE32(V3XCL_ITEM) == 64);\n    Cs->item = (V3XCL_ITEM*)V3X_READ_ALLOC(V3XCL_ITEM, Cs->numItem, -1, in);\n#ifdef NG_SDL2\n    { int ci; for (ci = 0; ci < Cs->numItem; ci++) if (Cs->item[ci].type == V3XCTYPE_MESH) SYS_Debug(\"[NG] WARNING: collision mesh item in data (32-bit pointers not converted)\\n\"); }\n#endif")
edit(SC, "    Mo = (V3XTWEEN*)v3x_read_alloc(sizeof(V3XTWEEN), 1, -1, in);", "    Mo = (V3XTWEEN*)V3X_READ_ALLOC(V3XTWEEN, 1, -1, in);")
edit(SC, "    V3XORI97\t*ori97A = (V3XORI97*)MM_std.malloc(pScene->numORI*sizeof(V3XORI97));\n    V3XORI97\t*ori97;\n    V3XORI\t\t*ori;\n    int i;\n    FIO_gzip.fread(ori97A, pScene->numORI, sizeof(V3XORI97), in);",
     "    V3XORI97\t*ori97A;\n    V3XORI97\t*ori97;\n    V3XORI\t\t*ori;\n    int i;\n#ifdef NG_SDL2\n    ori97A = (V3XORI97*)V3X_READ_ALLOC(V3XORI97, pScene->numORI, -1, in);\n#else\n    ori97A = (V3XORI97*)MM_std.malloc(pScene->numORI*sizeof(V3XORI97));\n    FIO_gzip.fread(ori97A, pScene->numORI, sizeof(V3XORI97), in);\n#endif")
edit(SC, "    MM_std.free(ori97A);\n    return;\n}", "#ifndef NG_SDL2\n    MM_std.free(ori97A);\n#endif\n    return;\n}")
edit(SC, "    V3XOVI97 *ovi97A = (V3XOVI97*)MM_std.malloc(pScene->numOVI*sizeof(V3XOVI97));\n    V3XOVI97  *ovi97;\n    V3XOVI *ovi;\n    int i;\n    FIO_gzip.fread(ovi97A, pScene->numOVI, sizeof(V3XOVI97), in);",
     "    V3XOVI97 *ovi97A;\n    V3XOVI97  *ovi97;\n    V3XOVI *ovi;\n    int i;\n#ifdef NG_SDL2\n    ovi97A = (V3XOVI97*)V3X_READ_ALLOC(V3XOVI97, pScene->numOVI, -1, in);\n#else\n    ovi97A = (V3XOVI97*)MM_std.malloc(pScene->numOVI*sizeof(V3XOVI97));\n    FIO_gzip.fread(ovi97A, pScene->numOVI, sizeof(V3XOVI97), in);\n#endif")
edit(SC, "    MM_std.free(ovi97A);\n    return;\n}", "#ifndef NG_SDL2\n    MM_std.free(ovi97A);\n#endif\n    return;\n}")
edit(SC, "\t\tpScene->ORI = (V3XORI*)v3x_read_alloc(sizeof(V3XORI), pScene->numORI, V3X.Setup.MaxExtentableObjet, in);", "\t\tpScene->ORI = (V3XORI*)V3X_READ_ALLOC(V3XORI, pScene->numORI, V3X.Setup.MaxExtentableObjet, in);")
edit(SC, "\t\tpScene->OVI = (V3XOVI*) v3x_read_alloc(sizeof(V3XOVI), pScene->numOVI, V3X.Setup.MaxExtentableObjet, in);\n    pScene->TRI = (V3XTRI*) v3x_read_alloc(sizeof(V3XTRI), pScene->numTRI, V3X.Setup.MaxExtentableObjet, in);\n    pScene->TVI = (V3XTVI*) v3x_read_alloc(sizeof(V3XTVI), pScene->numTVI, V3X.Setup.MaxExtentableObjet, in);",
     "\t\tpScene->OVI = (V3XOVI*) V3X_READ_ALLOC(V3XOVI, pScene->numOVI, V3X.Setup.MaxExtentableObjet, in);\n    pScene->TRI = (V3XTRI*) V3X_READ_ALLOC(V3XTRI, pScene->numTRI, V3X.Setup.MaxExtentableObjet, in);\n    pScene->TVI = (V3XTVI*) V3X_READ_ALLOC(V3XTVI, pScene->numTVI, V3X.Setup.MaxExtentableObjet, in);")
edit(SC, "            TRI->keyEx=(V3XKEYEX*)v3x_read_alloc(sizeof(V3XKEYEX), TRI->numKeys, -1, in);", "            TRI->keyEx=(V3XKEYEX*)V3X_READ_ALLOC(V3XKEYEX, TRI->numKeys, -1, in);")
edit(SC, "            TRI->keys=(V3XKEY*)v3x_read_alloc(sizeof(V3XKEY), TRI->numFrames, -1, in);", "            TRI->keys=(V3XKEY*)V3X_READ_ALLOC(V3XKEY, TRI->numFrames, -1, in);")
edit(SC, "            FIO_gzip.fread(ORI, 1, sizeof(V3XORI), in);", "            V3X_FREAD1(ORI, V3XORI, in);")
edit_all(SC, "        FIO_gzip.fread(TRI, 1, sizeof(V3XTRI), in);\n", "        V3X_FREAD1(TRI, V3XTRI, in);\n")
edit(SC, "    temp = (u_int8_t*) MM_std.malloc(HEAD1 + sizeof(V3XLAYER97));\n    FIO_gzip.fread(temp, HEAD1 + sizeof(V3XLAYER97), 1, in);\n    sysMemCpy(pScene, temp, HEAD1);",
     "#ifdef NG_SDL2\n#undef HEAD1\n#define HEAD1 (ng_l32_V3XSCENE.size32 - ng_l32_V3XLAYER.size32)\n    sysMemZero(pScene, sizeof(V3XSCENE));\n#endif\n    temp = (u_int8_t*) MM_std.malloc(HEAD1 + sizeof(V3XLAYER97));\n    FIO_gzip.fread(temp, HEAD1 + sizeof(V3XLAYER97), 1, in);\n#ifdef NG_SDL2\n    NG_Convert32(&ng_l32_V3XSCENE, pScene, temp, 1, HEAD1);\n#else\n    sysMemCpy(pScene, temp, HEAD1);\n#endif")

# Demo replays: SGRecordReplay / SGRecordBuffer hold pointers.
LS = "src/lt_save.c"
edit(LS, "    pIO->fread(&g_pRecordData, sizeof(SGRecordReplay), 1, in);\n",
     "#ifdef NG_SDL2\n    {\n        u_int8_t tmp32[sizeof(SGRecordReplay)];\n        pIO->fread(tmp32, ng_l32_SGRecordReplay.size32, 1, in);\n        sysMemZero(&g_pRecordData, sizeof(SGRecordReplay));\n        NG_Convert32(&ng_l32_SGRecordReplay, &g_pRecordData, tmp32, 1, ng_l32_SGRecordReplay.size32);\n    }\n#else\n    pIO->fread(&g_pRecordData, sizeof(SGRecordReplay), 1, in);\n#endif\n")
edit(LS, "        pIO->fread(g_pRecordData.rec, sizeof(SGRecordBuffer), g_pRecordData.step, in);\n",
     "#ifdef NG_SDL2\n        {\n            u_int8_t *tmp32 = (u_int8_t*)MM_std.malloc(ng_l32_SGRecordBuffer.size32 * g_pRecordData.step);\n            pIO->fread(tmp32, ng_l32_SGRecordBuffer.size32, g_pRecordData.step, in);\n            NG_Convert32(&ng_l32_SGRecordBuffer, g_pRecordData.rec, tmp32, g_pRecordData.step, ng_l32_SGRecordBuffer.size32);\n            MM_std.free(tmp32);\n        }\n#else\n        pIO->fread(g_pRecordData.rec, sizeof(SGRecordBuffer), g_pRecordData.step, in);\n#endif\n")
edit(LS, "#include \"sysini.h\"\n", "#include \"sysini.h\"\n#ifdef NG_SDL2\n#include \"ng_conv32.h\"\n#endif\n")

# FLI animated textures keep the in-memory buffer pointer in a u_int32_t (start_buf).
edit("rlx32/include/gx_flc.h", "    u_int32_t       start_buf;\n", "#ifdef NG_SDL2\n    uintptr_t       start_buf;\n#else\n    u_int32_t       start_buf;\n#endif\n")
edit("rlx32/src/gx_flc.c", "        pAnim->start_buf = (u_int32_t)pAnim->fileBuffer;\n", "        pAnim->start_buf = (uintptr_t)pAnim->fileBuffer;\n")

# GLES renderer path (Quest): ng_gles.h emulates the GL 1.x slice the renderer uses.
edit("rlx32/src/renderers/opengl/gl_v3x.h", "#elif (defined __LINUX__)\n#define GL_GLEXT_PROTOTYPES\n#include <GL/gl.h>\n#include <GL/glext.h>\n",
     "#elif (defined __LINUX__) && defined(NG_GLES)\n#include \"ng_gles.h\"\n\n#elif (defined __LINUX__)\n#define GL_GLEXT_PROTOTYPES\n#include <GL/gl.h>\n#include <GL/glext.h>\n")

# Quest: the joystick driver must open the virtual Touch joystick, not the Quest's own
# Android gamepad devices (they never send input in an immersive OpenXR app).
edit("rlx32/src/linux/sysjoy.c", "    // Open the device.  If this fails, just skip it.\n    joy = SDL_JoystickOpen(idx);\n",
     "#ifdef NG_QUEST\n    {\n      extern int NGQ_IsVirtualJoystick(int index);\n      if (!NGQ_IsVirtualJoystick(idx)) continue;\n    }\n#endif\n    // Open the device.  If this fails, just skip it.\n    joy = SDL_JoystickOpen(idx);\n")

# Quest: the virtual screen has exactly one mode (1024x768, see NG_ListModes). The menus
# only draw sprites/3D when the configured resolution matches the active mode, so force it.
edit("src/lt_info.c", "        g_SGSettings.OS = RLX.System.Id;        \n        NG_SaveGameInfo();\n    }\n    return;\n}",
     "        g_SGSettings.OS = RLX.System.Id;        \n        NG_SaveGameInfo();\n    }\n#ifdef NG_QUEST\n    g_SGSettings.ResolutionX = 1024;\n    g_SGSettings.ResolutionY = 768;\n    g_SGSettings.ColorDepth = 32;\n#endif\n    return;\n}")

print("changed:", sorted(set(changed)) or "nothing (already applied)")
