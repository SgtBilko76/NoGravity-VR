# No Gravity VR for Meta Quest and PICO

**No Gravity** (realtech VR, 1997, released as open source under the GPL) as a standalone
VR game for **Meta Quest 3** and **PICO 4 / Neo 3** (OpenXR, one APK for both). The whole game is inside the APK: no PC, no extra files.

- Menus on a large floating screen in front of you
- Flight in real stereo 3D with head tracking: look around the cockpit, ships and
  explosions have real depth
- HUD and crosshair fixed in front of you, lined up with the ship's guns
- Hit and explosion flashes fill your whole view
- Touch controllers work as a flight stick (analog pitch, turn, roll and throttle)
- On-screen keyboard for the pilot name
- 2400 x 2400 per eye with 4x anti-aliasing, 72 Hz

## Install

1. Download `NoGravity-VR-v0.1.1-beta.apk` from the
   [Releases](https://github.com/SgtBilko76/NoGravity-VR/releases) page.
2. Install it with SideQuest (drag the APK onto the window) or with
   `adb install -r NoGravity-VR-v0.1.1-beta.apk`.
3. On the Quest: **Library > Unknown Sources > No Gravity VR**. On PICO use adb or the sideloader you
   already use (SideQuest is a Quest tool); the game is then in the PICO library.

The first start takes a few seconds longer (the game data is unpacked once).

## Controls

| Button | Menus | Flight |
|---|---|---|
| Left stick | Move the selection | Pitch and turn |
| Right stick left / right | Move the selection | Roll |
| Right stick up / down | Move the selection | Throttle lever (stays where you leave it) |
| A | Select | Lock target |
| B | Back | Next weapon |
| X | – | Next item |
| Y | – | Next navpoint |
| Right trigger | Select | Fire |
| Left trigger | – | Missile / use item |
| Right grip | – | Booster |
| Left grip | – | Reverse booster |
| Right stick click | – | HUD info |
| Left stick click | – | Radar mode |
| ☰ (left controller) | Back / Quit question | Pause menu |
| Both sticks pressed for 1 s | Recentre the view | Recentre the view |

The flight buttons can be changed in the game's own joystick options.

**Pilot name keyboard:** stick = move, A or trigger = type, B = delete, X = space,
Y = OK, ☰ = cancel.

**Quit:** main menu > ☰ > Yes. This closes the app and returns to the Quest home.

## Notes

- Tested on Quest 3. Quest 2 / 3S / Pro should work but are untested.
- **PICO 4 / Neo 3 (untested):** the same APK supports the PICO controllers (same button layout as the
  Quest controllers, ☰ = the PICO menu button on the left controller). PICO needs PICO OS 5.9.0 or newer.
  It is not tested on PICO hardware yet: if the game does not start or the controllers do nothing,
  please open an issue with the output of `adb logcat -s NoGravityVR:*`.
- Settings and pilots are kept in the app's own storage
  (`/data/data/com.bilko.nogravityvr/files/.realtech/nogravity`).
- Optional tuning without reinstalling: a text file
  `/sdcard/Android/data/com.bilko.nogravityvr/files/env.txt` with lines such as
  `NGQ_EYE_SIZE=2000` (lower resolution) or `NGQ_MSAA=0` (no anti-aliasing).
- Log: `adb logcat -s NoGravityVR:*`

## Building from source

The VR port lives in `port/` (the game sources in `source/` are realtech's, changed only by
the re-runnable script `port/tools/sdl2_port.py`).

- `port/CMakeLists.txt`: desktop (SDL2 + OpenGL or GLES) and Android builds
- `port/sdl2/`: SDL 1.2 -> SDL2 platform layer
- `port/gles/`: OpenGL ES 3 replacement for the fixed-function renderer
- `port/gen/`: generated 32-bit -> 64-bit layout tables for the game's data files
- `port/quest/`: OpenXR host (`src/ng_quest.cpp`), stereo flight (`src/ng_vr_game.c`),
  on-screen keyboard (`src/ng_vkbd.c`), Android app (`android/`)

Requirements: Android SDK with NDK 27.2.12479018 and CMake 3.22.1, JDK 17, vcpkg.

```sh
# Android libraries (SDL2, SDL2_mixer, libpng, libvorbis) for arm64
cd port
VCPKG_ROOT=/path/to/vcpkg ANDROID_NDK_HOME=/path/to/ndk/27.2.12479018 \
  $VCPKG_ROOT/vcpkg install --triplet arm64-android \
  --x-manifest-root=vcpkg/android --x-install-root=deps-android

# APK
cd quest/android
echo "sdk.dir=/path/to/android-sdk" > local.properties
./gradlew assembleRelease     # -> app/build/outputs/apk/release/app-release.apk
```

Desktop test builds (Linux): `port/vcpkg/desktop` + `cmake -S port -B build/linux ...`,
with `-DNG_GLES=ON -DNG_FAKE_VR=ON` for a desktop preview of the stereo flight
(both eyes side by side).

## Credits and license

- **No Gravity** by realtech VR (Stéphane Denis), GPL: https://sourceforge.net/projects/nogravity/
- VR port by Sgt.Bilko: https://portroyale.online
- SDL2 / SDL2_mixer (zlib license), libpng, libogg/libvorbis (BSD), Khronos OpenXR loader (Apache 2.0)

The code is GPL-3.0 (see `LICENSE`); the game data `Bin/NOGRAVITY.RMX` is GPL-2.0 (see `Bin/GNU.TXT`).
