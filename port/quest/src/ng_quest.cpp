/* NoGravity VR - Quest host.
 *
 * Stage 1: the game renders its normal 2D/3D frame into an off-screen FBO (the "panel",
 * the engine's framebuffer 0 via NGL_SetTargetFramebuffer). Each game present blits the
 * panel into an OpenXR quad layer: a big virtual screen floating in front of the player.
 * Touch controllers drive an SDL virtual joystick the game opens like a PC joystick
 * (left stick = stick, right stick Y = throttle axis, buttons remappable in the game's
 * own joystick options) plus A = Enter, B = Escape for the menus.
 *
 * Recentre: hold both thumbstick clicks for 1 s (or touch <files>/recenter).
 * Logcat tag: NoGravityVR (stdout/stderr mirrored).
 */
#include "ng_quest.h"

#include <SDL.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <jni.h>
#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unwind.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" void NGL_SetTargetFramebuffer(GLuint fbo, int w, int h);
extern "C" void NGL_PushScreen(int w, int h);
extern "C" void NGL_PopScreen(void);

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif
#ifndef GL_FRAMEBUFFER_SRGB_EXT
#define GL_FRAMEBUFFER_SRGB_EXT 0x8DB9
#endif

static const char *TAG = "NoGravityVR";
#define XRLOG(...) __android_log_print(ANDROID_LOG_INFO, TAG, "[XR] " __VA_ARGS__)

/* ------------------------------------------------------------------ boot */
namespace {
std::atomic<int> s_crashing{0};
int s_frames = 0;

_Unwind_Reason_Code unwindCb(struct _Unwind_Context *ctx, void *) {
    uintptr_t pc = _Unwind_GetIP(ctx);
    if (!pc) return _URC_END_OF_STACK;
    Dl_info info;
    if (dladdr((void *)pc, &info) && info.dli_fname) {
        const char *base = strrchr(info.dli_fname, '/');
        base = base ? base + 1 : info.dli_fname;
        __android_log_print(ANDROID_LOG_FATAL, TAG, "  #%02d pc 0x%08lx %s (%s)", s_frames, (unsigned long)(pc - (uintptr_t)info.dli_fbase), base,
                            info.dli_sname ? info.dli_sname : "?");
    } else {
        __android_log_print(ANDROID_LOG_FATAL, TAG, "  #%02d pc 0x%016lx ???", s_frames, (unsigned long)pc);
    }
    return ++s_frames >= 48 ? _URC_END_OF_STACK : _URC_NO_REASON;
}

void onCrash(int sig, siginfo_t *si, void *) {
    int expected = 0;
    if (!s_crashing.compare_exchange_strong(expected, 1)) for (;;) pause();
    __android_log_print(ANDROID_LOG_FATAL, TAG, "*** native crash: signal %d code %d addr %p ***", sig, si ? si->si_code : 0, si ? si->si_addr : nullptr);
    s_frames = 0;
    _Unwind_Backtrace(unwindCb, nullptr);
    __android_log_write(ANDROID_LOG_FATAL, TAG, "*** end of backtrace ***");
    _exit(128 + sig);
}

void installCrashHandler() {
    static char altstack[64 * 1024];
    stack_t ss; ss.ss_sp = altstack; ss.ss_size = sizeof altstack; ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = onCrash; sa.sa_flags = SA_SIGINFO | SA_ONSTACK; sigemptyset(&sa.sa_mask);
    const int sigs[] = {SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE};
    for (int s : sigs) sigaction(s, &sa, nullptr);
}

void *logThread(void *arg) {
    FILE *f = fdopen((int)(intptr_t)arg, "r");
    char line[2048];
    while (f && fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        if (n && line[n - 1] == '\n') line[n - 1] = 0;
        __android_log_write(ANDROID_LOG_INFO, TAG, line);
    }
    return nullptr;
}

void mirrorStdio() {
    int fds[2];
    if (pipe(fds) != 0) return;
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    pthread_t t;
    pthread_create(&t, nullptr, logThread, (void *)(intptr_t)fds[0]);
    pthread_detach(t);
}

/* Copy an APK asset to dst unless dst already has the same size. */
bool extractAsset(const char *name, const std::string &dst) {
    SDL_RWops *rw = SDL_RWFromFile(name, "rb");
    if (!rw) { __android_log_print(ANDROID_LOG_ERROR, TAG, "asset %s missing: %s", name, SDL_GetError()); return false; }
    Sint64 size = SDL_RWsize(rw);
    struct stat st;
    if (stat(dst.c_str(), &st) == 0 && st.st_size == size) { SDL_RWclose(rw); return true; }
    std::string tmp = dst + ".part";
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f) { SDL_RWclose(rw); return false; }
    std::vector<char> buf(1 << 20);
    size_t got;
    while ((got = SDL_RWread(rw, buf.data(), 1, buf.size())) > 0) fwrite(buf.data(), 1, got, f);
    fclose(f);
    SDL_RWclose(rw);
    rename(tmp.c_str(), dst.c_str());
    __android_log_print(ANDROID_LOG_INFO, TAG, "extracted %s (%lld bytes)", name, (long long)size);
    return true;
}

std::string s_filesDir;
SDL_Joystick *s_vjoy = nullptr;
int s_vjoyIndex = -1;
} // namespace

extern "C" void NGQ_Boot(void) {
    installCrashHandler();
    mirrorStdio();
    /* keep the game loop (and xrPollEvent) running while the activity is paused */
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
    SDL_SetHint(SDL_HINT_TV_REMOTE_AS_JOYSTICK, "0");
    const char *internal = SDL_AndroidGetInternalStoragePath();
    if (internal) {
        s_filesDir = internal;
        mkdir(s_filesDir.c_str(), 0755);
        chdir(s_filesDir.c_str());
        /* the Linux preferences code keeps saves/config in $HOME/.realtech/nogravity */
        setenv("HOME", s_filesDir.c_str(), 1);
        __android_log_print(ANDROID_LOG_INFO, TAG, "cwd = %s", s_filesDir.c_str());
        extractAsset("NOGRAVITY.RMX", s_filesDir + "/NOGRAVITY.RMX");
    }
}

/* Touch controllers as a PC joystick for the game (sysjoy.c opens exactly this one).
   Must exist before the engine enumerates joysticks. */
extern "C" void NGQ_CreateVirtualJoystick(void) {
    SDL_InitSubSystem(SDL_INIT_JOYSTICK);
    s_vjoyIndex = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 4, 12, 0);
    if (s_vjoyIndex >= 0) s_vjoy = SDL_JoystickOpen(s_vjoyIndex);
    XRLOG("virtual joystick index %d %s", s_vjoyIndex, s_vjoy ? "open" : SDL_GetError());
}

/* ------------------------------------------------------------------ OpenXR state */
namespace {
struct Swapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    int32_t w = 0, h = 0;
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    std::vector<GLuint> fbos;
};

int s_w = 1024, s_h = 768;              /* game frame (panel) size */
GLuint s_panelFbo = 0, s_panelTex = 0, s_panelDepth = 0;

XrInstance s_inst = XR_NULL_HANDLE;
XrSystemId s_sys = XR_NULL_SYSTEM_ID;
XrSession s_sess = XR_NULL_HANDLE;
XrSpace s_baseSpace = XR_NULL_HANDLE, s_appSpace = XR_NULL_HANDLE, s_viewSpace = XR_NULL_HANDLE, s_pendingSpace = XR_NULL_HANDLE;
XrSessionState s_state = XR_SESSION_STATE_UNKNOWN;
bool s_running = false, s_lost = false, s_inBackground = false;
XrFrameState s_frame{XR_TYPE_FRAME_STATE};
int64_t s_colorFormat = GL_RGBA8;
bool s_srgbWriteControl = false;
Swapchain s_panelSc;
/* stereo flight */
Swapchain s_eyeSc[2];
GLuint s_eyeDepth[2] = {0, 0};
int s_eyeSize = 1440;
XrView s_views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
float s_eyeTan = 1.2f;
bool s_stereoFrame = false, s_stereoRender = false, s_eyesValid = false;
uint32_t s_eyeIdx[2] = {0, 0};
float s_hudL = -0.8f, s_hudR = 0.8f, s_hudU = 0.6f, s_hudD = -0.6f;
const float kHudDist = 3.0f;
int s_frameKind = 0, s_lastKind = -1; /* 1 flat, 2 stereo (logged on change) */
XrEnvironmentBlendMode s_blend = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
bool s_recenterPending = true, s_recenterDone = false, s_recenterPosOnly = false;
float s_recenterYaw = 0.f;
uint32_t s_recenterAt = 0; /* delayed recentre (SDL ticks), 0 = none */
uint64_t s_frameNo = 0;
JavaVM *s_vm = nullptr;
jobject s_activity = nullptr;
PFN_xrRequestDisplayRefreshRateFB s_reqRefresh = nullptr;
PFN_xrEnumerateDisplayRefreshRatesFB s_enumRefresh = nullptr;
PFN_xrPerfSettingsSetPerformanceLevelEXT s_perf = nullptr;

/* virtual screen: 4:3, 2.6 m wide, 2.4 m in front, slightly below eye level */
const float kPanelDist = 2.4f, kPanelWidth = 2.6f, kPanelY = -0.1f;

/* input */
XrActionSet s_actSet = XR_NULL_HANDLE;
XrAction s_aStick[2], s_aStickX[2], s_aStickY[2], s_aTrigger[2], s_aGrip[2], s_aClick[2];
XrAction s_aA, s_aB, s_aX, s_aY, s_aMenu;
XrPath s_hand[2];
uint32_t s_recenterHoldStart = 0;
bool s_prevA = false, s_prevB = false, s_prevMenu = false;
float s_throttle = 0.5f;
SDL_Keycode s_menuDir = 0;
uint32_t s_menuRepeat = 0;
std::atomic<uint32_t> s_lastFlightTick{0};
std::atomic<int> s_textInput{0};

const char *stateName(XrSessionState s) {
    switch (s) {
    case XR_SESSION_STATE_IDLE: return "IDLE";
    case XR_SESSION_STATE_READY: return "READY";
    case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
    case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
    case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
    case XR_SESSION_STATE_STOPPING: return "STOPPING";
    case XR_SESSION_STATE_EXITING: return "EXITING";
    case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
    default: return "UNKNOWN";
    }
}

bool check(XrResult r, const char *what) {
    if (XR_SUCCEEDED(r)) return true;
    char buf[XR_MAX_RESULT_STRING_SIZE] = "?";
    if (s_inst) xrResultToString(s_inst, r, buf);
    XRLOG("%s failed: %s (%d)", what, buf, (int)r);
    return false;
}

bool createSwapchain(Swapchain &sc, int w, int h) {
    sc.w = w; sc.h = h;
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.arraySize = 1; ci.mipCount = 1; ci.faceCount = 1; ci.sampleCount = 1;
    ci.format = s_colorFormat; ci.width = w; ci.height = h;
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    if (!check(xrCreateSwapchain(s_sess, &ci, &sc.handle), "xrCreateSwapchain")) return false;
    uint32_t n = 0;
    xrEnumerateSwapchainImages(sc.handle, 0, &n, nullptr);
    sc.images.assign(n, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
    xrEnumerateSwapchainImages(sc.handle, n, &n, (XrSwapchainImageBaseHeader *)sc.images.data());
    sc.fbos.assign(n, 0);
    glGenFramebuffers(n, sc.fbos.data());
    for (uint32_t i = 0; i < n; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, sc.fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sc.images[i].image, 0);
    }
    XRLOG("swapchain %dx%d, %u images", w, h, n);
    return true;
}

XrPath path(const char *s) { XrPath p = XR_NULL_PATH; xrStringToPath(s_inst, s, &p); return p; }

XrAction makeAction(const char *name, XrActionType type, bool perHand) {
    XrActionCreateInfo ai{XR_TYPE_ACTION_CREATE_INFO};
    strncpy(ai.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
    strncpy(ai.localizedActionName, name, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    ai.actionType = type;
    if (perHand) { ai.countSubactionPaths = 2; ai.subactionPaths = s_hand; }
    XrAction a = XR_NULL_HANDLE;
    check(xrCreateAction(s_actSet, &ai, &a), name);
    return a;
}

void initInput() {
    s_hand[0] = path("/user/hand/left");
    s_hand[1] = path("/user/hand/right");
    XrActionSetCreateInfo si{XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy(si.actionSetName, "nogravity");
    strcpy(si.localizedActionSetName, "No Gravity");
    if (!check(xrCreateActionSet(s_inst, &si, &s_actSet), "xrCreateActionSet")) return;
    const char *hn[2] = {"left", "right"};
    for (int h = 0; h < 2; h++) {
        char n[64];
        snprintf(n, sizeof n, "stick_%s", hn[h]); s_aStick[h] = makeAction(n, XR_ACTION_TYPE_VECTOR2F_INPUT, false);
        snprintf(n, sizeof n, "stickx_%s", hn[h]); s_aStickX[h] = makeAction(n, XR_ACTION_TYPE_FLOAT_INPUT, false);
        snprintf(n, sizeof n, "sticky_%s", hn[h]); s_aStickY[h] = makeAction(n, XR_ACTION_TYPE_FLOAT_INPUT, false);
        snprintf(n, sizeof n, "trigger_%s", hn[h]); s_aTrigger[h] = makeAction(n, XR_ACTION_TYPE_FLOAT_INPUT, false);
        snprintf(n, sizeof n, "grip_%s", hn[h]); s_aGrip[h] = makeAction(n, XR_ACTION_TYPE_FLOAT_INPUT, false);
        snprintf(n, sizeof n, "click_%s", hn[h]); s_aClick[h] = makeAction(n, XR_ACTION_TYPE_BOOLEAN_INPUT, false);
    }
    s_aA = makeAction("button_a", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
    s_aB = makeAction("button_b", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
    s_aX = makeAction("button_x", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
    s_aY = makeAction("button_y", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
    s_aMenu = makeAction("button_menu", XR_ACTION_TYPE_BOOLEAN_INPUT, false);

    std::vector<XrActionSuggestedBinding> b;
    for (int h = 0; h < 2; h++) {
        std::string p = std::string("/user/hand/") + hn[h] + "/input/";
        b.push_back({s_aStick[h], path((p + "thumbstick").c_str())});
        b.push_back({s_aStickX[h], path((p + "thumbstick/x").c_str())});
        b.push_back({s_aStickY[h], path((p + "thumbstick/y").c_str())});
        b.push_back({s_aTrigger[h], path((p + "trigger/value").c_str())});
        b.push_back({s_aGrip[h], path((p + "squeeze/value").c_str())});
        b.push_back({s_aClick[h], path((p + "thumbstick/click").c_str())});
    }
    b.push_back({s_aA, path("/user/hand/right/input/a/click")});
    b.push_back({s_aB, path("/user/hand/right/input/b/click")});
    b.push_back({s_aX, path("/user/hand/left/input/x/click")});
    b.push_back({s_aY, path("/user/hand/left/input/y/click")});
    b.push_back({s_aMenu, path("/user/hand/left/input/menu/click")});
    XrInteractionProfileSuggestedBinding sb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    sb.interactionProfile = path("/interaction_profiles/oculus/touch_controller");
    sb.suggestedBindings = b.data();
    sb.countSuggestedBindings = (uint32_t)b.size();
    check(xrSuggestInteractionProfileBindings(s_inst, &sb), "xrSuggestInteractionProfileBindings");
    XrSessionActionSetsAttachInfo at{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    at.countActionSets = 1;
    at.actionSets = &s_actSet;
    check(xrAttachSessionActionSets(s_sess, &at), "xrAttachSessionActionSets");
}

float getFloat(XrAction a) {
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO}; gi.action = a;
    XrActionStateFloat st{XR_TYPE_ACTION_STATE_FLOAT};
    if (XR_FAILED(xrGetActionStateFloat(s_sess, &gi, &st)) || !st.isActive) return 0.f;
    return st.currentState;
}
bool getBool(XrAction a) {
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO}; gi.action = a;
    XrActionStateBoolean st{XR_TYPE_ACTION_STATE_BOOLEAN};
    if (XR_FAILED(xrGetActionStateBoolean(s_sess, &gi, &st)) || !st.isActive) return false;
    return st.currentState == XR_TRUE;
}
XrVector2f getStick(int h) {
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO}; gi.action = s_aStick[h];
    XrActionStateVector2f st{XR_TYPE_ACTION_STATE_VECTOR2F};
    XrVector2f v{0, 0};
    if (XR_SUCCEEDED(xrGetActionStateVector2f(s_sess, &gi, &st)) && st.isActive) v = st.currentState;
    /* the vector2f binding can report 0,0 on Quest; fall back to the x/y components */
    if (v.x == 0.f && v.y == 0.f) { v.x = getFloat(s_aStickX[h]); v.y = getFloat(s_aStickY[h]); }
    return v;
}

void sendKey(SDL_Keycode k, bool down) {
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    e.key.keysym.sym = k;
    e.key.keysym.scancode = SDL_GetScancodeFromKey(k);
    SDL_PushEvent(&e);
}

Sint16 axis(float v) {
    if (v > 1.f) v = 1.f;
    if (v < -1.f) v = -1.f;
    return (Sint16)(v * 32767.f);
}

void updateInput() {
    if (!s_actSet || !s_running) return;
    bool focused = s_state == XR_SESSION_STATE_FOCUSED;
    XrActiveActionSet as{s_actSet, XR_NULL_PATH};
    XrActionsSyncInfo sy{XR_TYPE_ACTIONS_SYNC_INFO};
    sy.countActiveActionSets = 1;
    sy.activeActionSets = &as;
    if (!focused || XR_FAILED(xrSyncActions(s_sess, &sy))) return;

    XrVector2f l = getStick(0), r = getStick(1);
    float lt = getFloat(s_aTrigger[0]), rt = getFloat(s_aTrigger[1]);
    float lg = getFloat(s_aGrip[0]), rg = getFloat(s_aGrip[1]);
    bool a = getBool(s_aA), b = getBool(s_aB), x = getBool(s_aX), y = getBool(s_aY), menu = getBool(s_aMenu);
    bool lc = getBool(s_aClick[0]), rc = getBool(s_aClick[1]);

    const uint32_t now = SDL_GetTicks();
    const bool flying = (now - s_lastFlightTick) < 250;
    static bool wasFlying = false;
    if (flying && !wasFlying && !s_recenterPending) {
        /* new flight: put the eyes back on the ship's gun line (head height drifts while in the menus) */
        s_recenterPending = true;
        s_recenterPosOnly = true;
    }
    wasFlying = flying;
    static uint32_t lastT = 0;
    float dt = lastT ? (now - lastT) / 1000.f : 0.f;
    if (dt > 0.1f) dt = 0.1f;
    lastT = now;

    /* throttle lever: right stick Y moves it, it stays where you leave it (0 = stop, 1 = full) */
    float ry = fabsf(r.y) > 0.3f ? r.y : 0.f;
    if (flying) s_throttle += ry * dt * 0.7f;
    if (s_throttle < 0.f) s_throttle = 0.f;
    if (s_throttle > 1.f) s_throttle = 1.f;
    float rx = fabsf(r.x) > 0.25f ? r.x : 0.f;

    if (s_vjoy) {
        /* SDL joystick Y is +down; Touch stick Y is +up */
        SDL_JoystickSetVirtualAxis(s_vjoy, 0, axis(l.x));
        SDL_JoystickSetVirtualAxis(s_vjoy, 1, axis(-l.y));
        /* game: throttle = 65535 - lZ, lZ = axis + 32768 */
        SDL_JoystickSetVirtualAxis(s_vjoy, 2, (Sint16)(32767.f - 65535.f * s_throttle));
        SDL_JoystickSetVirtualAxis(s_vjoy, 3, axis(-rx));
        const bool btn[12] = {a, b, rt > 0.45f, lt > 0.45f, x, y, rg > 0.45f, lg > 0.45f, rc, lc, menu, false};
        for (int i = 0; i < 12; i++) SDL_JoystickSetVirtualButton(s_vjoy, i, btn[i]);
    }
    /* menu button = Escape (in flight: pause menu; on-screen keyboard: cancel) */
    if (menu != s_prevMenu) {
        sendKey(SDLK_ESCAPE, menu);
        XRLOG("menu button %s (flying %d, keyboard %d) -> Escape", menu ? "down" : "up", (int)flying, (int)s_textInput);
    }
    /* menus only: A = Enter, B = Escape, either stick = arrow keys (with repeat);
       the on-screen keyboard reads the joystick itself */
    if (!flying && !s_textInput) {
        if (a != s_prevA) sendKey(SDLK_RETURN, a);
        if (b != s_prevB) sendKey(SDLK_ESCAPE, b);
        float sx = fabsf(l.x) > fabsf(r.x) ? l.x : r.x, sy = fabsf(l.y) > fabsf(r.y) ? l.y : r.y;
        SDL_Keycode dir = 0;
        if (fabsf(sy) > 0.6f && fabsf(sy) >= fabsf(sx)) dir = sy > 0 ? SDLK_UP : SDLK_DOWN;
        else if (fabsf(sx) > 0.6f) dir = sx > 0 ? SDLK_RIGHT : SDLK_LEFT;
        if (dir != s_menuDir) {
            if (s_menuDir) sendKey(s_menuDir, false);
            if (dir) { sendKey(dir, true); s_menuRepeat = now + 450; }
            s_menuDir = dir;
        } else if (dir && now >= s_menuRepeat) {
            sendKey(dir, false); sendKey(dir, true); s_menuRepeat = now + 160;
        }
    } else {
        if (s_prevA && !a) sendKey(SDLK_RETURN, false);
        if (s_prevB && !b) sendKey(SDLK_ESCAPE, false);
        if (s_menuDir) { sendKey(s_menuDir, false); s_menuDir = 0; }
    }
    s_prevA = a; s_prevB = b; s_prevMenu = menu;

    /* left stick short click = Escape (pause menu in flight, back in menus): the Quest's left
       menu button is taken by the system on this headset (opens the Navigator). Not sent when the
       click is part of the two-stick recentre gesture or held long. */
    {
        static uint32_t lcDown = 0, escUpAt = 0;
        static bool lcCombo = false, prevLc = false;
        if (escUpAt && (int32_t)(now - escUpAt) >= 0) { sendKey(SDLK_ESCAPE, false); escUpAt = 0; }
        if (lc && !prevLc) { lcDown = now; lcCombo = rc; }
        if (lc && rc) lcCombo = true;
        if (!lc && prevLc && !lcCombo && now - lcDown < 600) {
            sendKey(SDLK_ESCAPE, true);
            escUpAt = now + 100; /* release later: down+up in one game frame would be missed */
            XRLOG("left stick click -> Escape (flying %d)", (int)flying);
        }
        prevLc = lc;
    }
    /* recentre: both stick clicks held 1 s */
    if (lc && rc) {
        if (!s_recenterHoldStart) s_recenterHoldStart = now;
        else if (s_recenterHoldStart != 0xffffffffu && now - s_recenterHoldStart > 1000) { s_recenterPending = true; s_recenterHoldStart = 0xffffffffu; }
    } else s_recenterHoldStart = 0;

    if (s_frameNo % 720 == 5)
        printf("[input] L(%.2f %.2f) R(%.2f %.2f) LT %.2f RT %.2f LG %.2f RG %.2f A%d B%d X%d Y%d M%d flying %d throttle %.2f\n", l.x, l.y, r.x, r.y, lt, rt, lg, rg, a, b, x, y, (int)menu, (int)flying, s_throttle);
}

void pollEvents() {
    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    while (s_inst) {
        ev = {XR_TYPE_EVENT_DATA_BUFFER};
        if (xrPollEvent(s_inst, &ev) != XR_SUCCESS) break;
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto *c = (XrEventDataSessionStateChanged *)&ev;
            s_state = c->state;
            XRLOG("session state -> %s", stateName(s_state));
            if (s_state == XR_SESSION_STATE_FOCUSED) {
                /* first start: now; headset put back on: after it has settled on the head */
                if (!s_recenterDone) s_recenterPending = true;
                else s_recenterAt = SDL_GetTicks() + 1500;
            }
            if (s_state == XR_SESSION_STATE_EXITING || s_state == XR_SESSION_STATE_LOSS_PENDING) s_lost = true;
        } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            s_lost = true;
        } else if (ev.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
            auto *rc = (XrEventDataReferenceSpaceChangePending *)&ev;
            /* system recentre / wake: fires while the headset is still being put on -> wait */
            if (rc->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL && s_recenterDone) s_recenterAt = SDL_GetTicks() + 1500;
        }
    }
    if (!s_running && s_state == XR_SESSION_STATE_READY) {
        XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
        bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        if (check(xrBeginSession(s_sess, &bi), "xrBeginSession")) {
            s_running = true;
            XRLOG("session begun");
            if (s_enumRefresh && s_reqRefresh) {
                uint32_t n = 0; s_enumRefresh(s_sess, 0, &n, nullptr);
                std::vector<float> rates(n); s_enumRefresh(s_sess, n, &n, rates.data());
                float best = 0; for (float r : rates) if (r <= 72.5f && r > best) best = r;
                if (best > 0) s_reqRefresh(s_sess, best);
            }
            if (s_perf) {
                s_perf(s_sess, XR_PERF_SETTINGS_DOMAIN_CPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
                s_perf(s_sess, XR_PERF_SETTINGS_DOMAIN_GPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
            }
        }
    }
    if (s_running && s_state == XR_SESSION_STATE_STOPPING) {
        xrEndSession(s_sess);
        s_running = false;
        XRLOG("session ended");
    }
}

void tryRecenter() {
    if (s_recenterAt && (int32_t)(SDL_GetTicks() - s_recenterAt) >= 0) { s_recenterAt = 0; s_recenterPending = true; s_recenterPosOnly = false; }
    if (!s_recenterPending || !s_viewSpace) return;
    XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(xrLocateSpace(s_viewSpace, s_baseSpace, s_frame.predictedDisplayTime, &loc))) return;
    const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if ((loc.locationFlags & need) != need) return;
    const XrQuaternionf q = loc.pose.orientation;
    const float fx = -(2.f * (q.x * q.z + q.w * q.y));
    const float fz = -(1.f - 2.f * (q.x * q.x + q.y * q.y));
    const float yaw = s_recenterPosOnly ? s_recenterYaw : atan2f(-fx, -fz);
    XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rs.poseInReferenceSpace.position = loc.pose.position;
    rs.poseInReferenceSpace.orientation = {0.f, sinf(yaw * 0.5f), 0.f, cosf(yaw * 0.5f)};
    XrSpace ns = XR_NULL_HANDLE;
    if (XR_FAILED(xrCreateReferenceSpace(s_sess, &rs, &ns))) return;
    if (s_pendingSpace) xrDestroySpace(s_pendingSpace);
    s_pendingSpace = ns;
    s_recenterPending = false;
    s_recenterDone = true;
    XRLOG("recentred %s(yaw %.1f deg, head at %.2f %.2f %.2f m)", s_recenterPosOnly ? "position only " : "", yaw * 57.2958f,
          loc.pose.position.x, loc.pose.position.y, loc.pose.position.z);
    s_recenterYaw = yaw;
    s_recenterPosOnly = false;
}

void checkRecenterMarker() {
    static uint32_t last = 0;
    uint32_t now = SDL_GetTicks();
    if (now - last < 1000) return;
    last = now;
    std::string m = s_filesDir + "/recenter";
    if (access(m.c_str(), F_OK) == 0) { unlink(m.c_str()); s_recenterPending = true; }
}

bool initOpenXR() {
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject act = (jobject)SDL_AndroidGetActivity();
    if (!env || !act) return false;
    env->GetJavaVM(&s_vm);
    s_activity = env->NewGlobalRef(act);
    env->DeleteLocalRef(act);

    PFN_xrInitializeLoaderKHR initLoader = nullptr;
    xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction *)&initLoader);
    if (!initLoader) return false;
    XrLoaderInitInfoAndroidKHR li{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    li.applicationVM = s_vm;
    li.applicationContext = s_activity;
    if (!check(initLoader((const XrLoaderInitInfoBaseHeaderKHR *)&li), "xrInitializeLoaderKHR")) return false;

    uint32_t n = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &n, nullptr);
    std::vector<XrExtensionProperties> exts(n, {XR_TYPE_EXTENSION_PROPERTIES});
    xrEnumerateInstanceExtensionProperties(nullptr, n, &n, exts.data());
    auto has = [&](const char *e) { for (auto &x : exts) if (!strcmp(x.extensionName, e)) return true; return false; };
    std::vector<const char *> en = {XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};
    if (has(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME)) en.push_back(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME);
    bool perf = has(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME), refresh = has(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
    if (perf) en.push_back(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);
    if (refresh) en.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);

    XrInstanceCreateInfoAndroidKHR ai{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    ai.applicationVM = s_vm;
    ai.applicationActivity = s_activity;
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    ci.next = &ai;
    strcpy(ci.applicationInfo.applicationName, "No Gravity VR");
    strcpy(ci.applicationInfo.engineName, "RLX");
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ci.enabledExtensionCount = (uint32_t)en.size();
    ci.enabledExtensionNames = en.data();
    if (!check(xrCreateInstance(&ci, &s_inst), "xrCreateInstance")) { s_inst = XR_NULL_HANDLE; return false; }
    if (perf) xrGetInstanceProcAddr(s_inst, "xrPerfSettingsSetPerformanceLevelEXT", (PFN_xrVoidFunction *)&s_perf);
    if (refresh) {
        xrGetInstanceProcAddr(s_inst, "xrRequestDisplayRefreshRateFB", (PFN_xrVoidFunction *)&s_reqRefresh);
        xrGetInstanceProcAddr(s_inst, "xrEnumerateDisplayRefreshRatesFB", (PFN_xrVoidFunction *)&s_enumRefresh);
    }

    XrSystemGetInfo sg{XR_TYPE_SYSTEM_GET_INFO};
    sg.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!check(xrGetSystem(s_inst, &sg, &s_sys), "xrGetSystem")) return false;
    PFN_xrGetOpenGLESGraphicsRequirementsKHR req = nullptr;
    xrGetInstanceProcAddr(s_inst, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&req);
    XrGraphicsRequirementsOpenGLESKHR gr{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    if (req) req(s_inst, s_sys, &gr);

    EGLDisplay dpy = eglGetCurrentDisplay();
    EGLContext ctx = eglGetCurrentContext();
    if (dpy == EGL_NO_DISPLAY || ctx == EGL_NO_CONTEXT) { XRLOG("no current EGL context"); return false; }
    EGLint cfgId = 0; eglQueryContext(dpy, ctx, EGL_CONFIG_ID, &cfgId);
    EGLConfig cfg = nullptr; EGLint cnt = 0;
    const EGLint attrs[] = {EGL_CONFIG_ID, cfgId, EGL_NONE};
    eglChooseConfig(dpy, attrs, &cfg, 1, &cnt);
    XrGraphicsBindingOpenGLESAndroidKHR gb{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    gb.display = dpy; gb.config = cfg; gb.context = ctx;
    XrSessionCreateInfo sc{XR_TYPE_SESSION_CREATE_INFO};
    sc.next = &gb;
    sc.systemId = s_sys;
    if (!check(xrCreateSession(s_inst, &sc, &s_sess), "xrCreateSession")) { s_sess = XR_NULL_HANDLE; return false; }

    XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rs.poseInReferenceSpace.orientation.w = 1.f;
    xrCreateReferenceSpace(s_sess, &rs, &s_baseSpace);
    xrCreateReferenceSpace(s_sess, &rs, &s_appSpace);
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    xrCreateReferenceSpace(s_sess, &rs, &s_viewSpace);

    uint32_t fc = 0;
    xrEnumerateSwapchainFormats(s_sess, 0, &fc, nullptr);
    std::vector<int64_t> fmts(fc);
    xrEnumerateSwapchainFormats(s_sess, fc, &fc, fmts.data());
    const char *glExts = (const char *)glGetString(GL_EXTENSIONS);
    s_srgbWriteControl = glExts && strstr(glExts, "GL_EXT_sRGB_write_control");
    bool hasSrgb = std::find(fmts.begin(), fmts.end(), (int64_t)GL_SRGB8_ALPHA8) != fmts.end();
    s_colorFormat = (hasSrgb && s_srgbWriteControl) ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    XRLOG("swapchain format 0x%llx (sRGB write control %d)", (unsigned long long)s_colorFormat, (int)s_srgbWriteControl);

    if (!createSwapchain(s_panelSc, s_w, s_h)) return false;
    {
        uint32_t vc = 0;
        xrEnumerateViewConfigurationViews(s_inst, s_sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &vc, nullptr);
        std::vector<XrViewConfigurationView> vcv(vc, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
        xrEnumerateViewConfigurationViews(s_inst, s_sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, vc, &vc, vcv.data());
        if (vc) XRLOG("recommended eye %ux%u", vcv[0].recommendedImageRectWidth, vcv[0].recommendedImageRectHeight);
        const char *es = getenv("NGQ_EYE_SIZE");
        if (es && atoi(es) >= 256) s_eyeSize = atoi(es);
        for (int e = 0; e < 2; e++) {
            if (!createSwapchain(s_eyeSc[e], s_eyeSize, s_eyeSize)) return false;
            glGenRenderbuffers(1, &s_eyeDepth[e]);
            glBindRenderbuffer(GL_RENDERBUFFER, s_eyeDepth[e]);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, s_eyeSize, s_eyeSize);
            for (GLuint fb : s_eyeSc[e].fbos) {
                glBindFramebuffer(GL_FRAMEBUFFER, fb);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s_eyeDepth[e]);
            }
        }
    }
    initInput();
    XRLOG("OpenXR ready");
    return true;
}

void bindPanel() {
    NGL_SetTargetFramebuffer(s_panelFbo, s_w, s_h);
    glViewport(0, 0, s_w, s_h);
}
} // namespace

/* ------------------------------------------------------------------ API */
extern "C" void NGQ_InitGL(int w, int h) {
    s_w = w; s_h = h;
    glGenTextures(1, &s_panelTex);
    glBindTexture(GL_TEXTURE_2D, s_panelTex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenRenderbuffers(1, &s_panelDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, s_panelDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glGenFramebuffers(1, &s_panelFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_panelFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_panelTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s_panelDepth);
    XRLOG("panel FBO %dx%d status 0x%x", w, h, glCheckFramebufferStatus(GL_FRAMEBUFFER));

    if (!initOpenXR()) XRLOG("OpenXR init failed - nothing will be visible");
    bindPanel();
}

extern "C" int NGQ_IsVirtualJoystick(int index) { return index == s_vjoyIndex; }

/* called by the game every flight frame (NG_ReadInput) */
extern "C" void NGQ_FlightTick(void) { s_lastFlightTick = SDL_GetTicks(); }
extern "C" void NGQ_SetTextInput(int on) { s_textInput = on; XRLOG("on-screen keyboard %s", on ? "open" : "closed"); }

namespace {
/* xrWaitFrame + xrBeginFrame + input, once per game frame */
bool beginXrFrame() {
    checkRecenterMarker();
    pollEvents();
    if (!s_running) return false;
    XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
    s_frame = {XR_TYPE_FRAME_STATE};
    if (!check(xrWaitFrame(s_sess, &wi, &s_frame), "xrWaitFrame")) return false;
    XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
    xrBeginFrame(s_sess, &bi);
    s_frameNo++;
    tryRecenter();
    updateInput();
    return true;
}

bool blitPanel(uint32_t *outIdx) {
    uint32_t idx = 0;
    XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (XR_FAILED(xrAcquireSwapchainImage(s_panelSc.handle, &ai, &idx))) return false;
    XrSwapchainImageWaitInfo sw{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    sw.timeout = XR_INFINITE_DURATION;
    xrWaitSwapchainImage(s_panelSc.handle, &sw);
    if (s_srgbWriteControl) glDisable(GL_FRAMEBUFFER_SRGB_EXT);
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, s_panelFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_panelSc.fbos[idx]);
    glBlitFramebuffer(0, 0, s_w, s_h, 0, 0, s_w, s_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(s_panelSc.handle, &ri);
    *outIdx = idx;
    return true;
}

void endXrFrame(bool stereo) {
    std::vector<const XrCompositionLayerBaseHeader *> layers;
    XrCompositionLayerProjection proj{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    XrCompositionLayerProjectionView pv[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    if (s_frame.shouldRender && !s_inBackground) {
        if (stereo && s_eyesValid) {
            const float a = atanf(s_eyeTan);
            for (int e = 0; e < 2; e++) {
                pv[e].pose = s_views[e].pose;
                pv[e].fov = {-a, a, a, -a};
                pv[e].subImage.swapchain = s_eyeSc[e].handle;
                pv[e].subImage.imageRect = {{0, 0}, {s_eyeSize, s_eyeSize}};
            }
            proj.space = s_appSpace;
            proj.viewCount = 2;
            proj.views = pv;
            layers.push_back((const XrCompositionLayerBaseHeader *)&proj);
        }
        uint32_t idx;
        if (blitPanel(&idx)) {
            quad.space = s_appSpace;
            quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quad.pose.orientation = {0, 0, 0, 1};
            quad.subImage.swapchain = s_panelSc.handle;
            quad.subImage.imageRect = {{0, 0}, {s_w, s_h}};
            if (stereo && s_eyesValid) {
                /* HUD: transparent layer at the game's own field of view, centred on the ship's nose */
                quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                quad.pose.position = {kHudDist * (s_hudL + s_hudR) * 0.5f, kHudDist * (s_hudU + s_hudD) * 0.5f, -kHudDist};
                quad.size = {kHudDist * (s_hudR - s_hudL), kHudDist * (s_hudU - s_hudD)};
            } else {
                quad.pose.position = {0.f, kPanelY, -kPanelDist};
                quad.size = {kPanelWidth, kPanelWidth * (float)s_h / (float)s_w};
            }
            layers.push_back((const XrCompositionLayerBaseHeader *)&quad);
        }
    }
    XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
    ei.displayTime = s_frame.predictedDisplayTime;
    ei.environmentBlendMode = s_blend;
    ei.layerCount = (uint32_t)layers.size();
    ei.layers = layers.empty() ? nullptr : layers.data();
    XrResult r = xrEndFrame(s_sess, &ei);
    if (XR_FAILED(r)) check(r, "xrEndFrame");
    s_frameKind = stereo ? 2 : 1;
    if (s_frameKind != s_lastKind) {
        XRLOG("now %s (eye %dx%d tan %.3f, HUD tan L %.3f R %.3f U %.3f D %.3f)", stereo ? "STEREO flight" : "flat screen", s_eyeSize, s_eyeSize, s_eyeTan, s_hudL, s_hudR, s_hudU, s_hudD);
        s_lastKind = s_frameKind;
    }
    if (s_frameNo % 720 == 1) XRLOG("frame %llu layers %u state %s", (unsigned long long)s_frameNo, ei.layerCount, stateName(s_state));
    if (s_pendingSpace) {
        if (s_appSpace) xrDestroySpace(s_appSpace);
        s_appSpace = s_pendingSpace;
        s_pendingSpace = XR_NULL_HANDLE;
    }
}
} // namespace

extern "C" int NGQ_StereoBegin(void) {
    s_stereoFrame = s_stereoRender = s_eyesValid = false;
    if (!s_sess || s_lost || !s_eyeSc[0].handle) return 0;
    if (getenv("NGQ_FLAT")) return 0;
    if (!beginXrFrame()) return 0;
    s_stereoFrame = true;
    if (!s_frame.shouldRender || s_inBackground) return 0; /* Present still ends the frame */
    XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = s_frame.predictedDisplayTime;
    li.space = s_appSpace;
    XrViewState vs{XR_TYPE_VIEW_STATE};
    uint32_t n = 0;
    s_views[0] = {XR_TYPE_VIEW}; s_views[1] = {XR_TYPE_VIEW};
    if (XR_FAILED(xrLocateViews(s_sess, &li, &vs, 2, &n, s_views)) || n != 2) return 0;
    if (!(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) return 0;
    float t = 0.f;
    for (int e = 0; e < 2; e++) {
        const XrFovf &f = s_views[e].fov;
        t = std::max(t, std::max(std::max(tanf(-f.angleLeft), tanf(f.angleRight)), std::max(tanf(f.angleUp), tanf(-f.angleDown))));
    }
    if (t > 0.3f && t < 4.f) s_eyeTan = t;
    s_stereoRender = true;
    return 1;
}

extern "C" int NGQ_StereoEyeBegin(int eye, float q[4], float p[3], float *tanHalf, int *w, int *h) {
    if (!s_stereoRender || eye < 0 || eye > 1) return 0;
    Swapchain &sc = s_eyeSc[eye];
    XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (XR_FAILED(xrAcquireSwapchainImage(sc.handle, &ai, &s_eyeIdx[eye]))) return 0;
    XrSwapchainImageWaitInfo sw{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    sw.timeout = XR_INFINITE_DURATION;
    xrWaitSwapchainImage(sc.handle, &sw);
    if (s_srgbWriteControl) glDisable(GL_FRAMEBUFFER_SRGB_EXT);
    glDisable(GL_SCISSOR_TEST);
    NGL_SetTargetFramebuffer(sc.fbos[s_eyeIdx[eye]], s_eyeSize, s_eyeSize);
    NGL_PushScreen(s_eyeSize, s_eyeSize);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    const XrPosef &ps = s_views[eye].pose;
    q[0] = ps.orientation.x; q[1] = ps.orientation.y; q[2] = ps.orientation.z; q[3] = ps.orientation.w;
    p[0] = ps.position.x; p[1] = ps.position.y; p[2] = ps.position.z;
    *tanHalf = s_eyeTan;
    *w = *h = s_eyeSize;
    if (eye == 0 && s_frameNo % 720 == 3)
        XRLOG("head offset (left eye) %.3f %.3f %.3f m from the recentre point", p[0], p[1], p[2]);
    return 1;
}

extern "C" void NGQ_StereoEyeEnd(int eye) {
    NGL_PopScreen();
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(s_eyeSc[eye].handle, &ri);
    if (eye == 1) s_eyesValid = true;
}

extern "C" void NGQ_StereoHudBegin(float tl, float tr, float tu, float td) {
    if (tr - tl > 0.2f && tr - tl < 10.f && tu - td > 0.2f && tu - td < 10.f) { s_hudL = tl; s_hudR = tr; s_hudU = tu; s_hudD = td; }
    bindPanel();
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

extern "C" void NGQ_Present(void) {
    if (!s_sess || s_lost) { s_stereoFrame = false; bindPanel(); return; }
    if (s_stereoFrame) {
        endXrFrame(true);
        s_stereoFrame = s_stereoRender = false;
    } else {
        if (!beginXrFrame()) { SDL_Delay(s_running ? 0 : 16); bindPanel(); return; }
        endXrFrame(false);
    }
    /* next game frame draws into the panel again */
    bindPanel();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

/* ------------------------------------------------------------------ quit */
namespace {
void *quitWatchdog(void *) {
    sleep(4);
    __android_log_write(ANDROID_LOG_WARN, TAG, "quit watchdog: shutdown hung, exiting");
    _exit(0);
    return nullptr;
}
void finishActivity() {
    if (!s_activity) return;
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    if (!env) return;
    jclass c = env->GetObjectClass(s_activity);
    jmethodID m = env->GetMethodID(c, "finishAndRemoveTask", "()V");
    if (m) env->CallVoidMethod(s_activity, m);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(c);
}
} // namespace

/* main menu Quit chosen: game is about to save settings and shut down */
extern "C" void NGQ_QuitBegin(void) {
    XRLOG("game quit requested");
    pthread_t t;
    pthread_create(&t, nullptr, quitWatchdog, nullptr);
    pthread_detach(t);
}

/* game shut down: leave VR cleanly and close the app (back to the Quest home) */
extern "C" void NGQ_QuitEnd(void) {
    if (s_sess) {
        if (s_running) xrRequestExitSession(s_sess);
        xrDestroySession(s_sess);
        s_sess = XR_NULL_HANDLE;
    }
    if (s_inst) { xrDestroyInstance(s_inst); s_inst = XR_NULL_HANDLE; }
    XRLOG("OpenXR closed, finishing activity");
    finishActivity();
    usleep(300 * 1000);
    _exit(0);
}
