# NoGravity port build (SDL2): desktop Linux test build and Android/Quest libmain.so
cmake_minimum_required(VERSION 3.20)
project(NoGravityPort C CXX)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(RLX  ${ROOT}/source/rlx32)
set(SRC  ${ROOT}/source/src)
set(PORT ${CMAKE_CURRENT_SOURCE_DIR})

option(NG_GLES  "Render through the GLES 3 fixed-function compat layer" OFF)
option(NG_SOUND "Build the SDL_mixer sound backend" ON)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Old x86 code: char is signed there, keep it signed on ARM too; tolerate K&R-era C.
set(NG_WARN -fsigned-char -fno-strict-aliasing -Wno-implicit-function-declaration
    -Wno-incompatible-pointer-types -Wno-int-conversion -Wno-pointer-sign
    -Wno-deprecated-declarations -Wno-unused-result -Wno-format -Wno-parentheses
    -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -Wno-everything)
add_compile_options("$<$<COMPILE_LANGUAGE:C>:${NG_WARN};-include;${CMAKE_CURRENT_SOURCE_DIR}/compat/ng_c_prelude.h;-Werror=implicit-function-declaration>")
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-fsigned-char;-fno-strict-aliasing;-Wno-everything;-fpermissive;-include;${CMAKE_CURRENT_SOURCE_DIR}/compat/ng_cxx_prelude.h>")
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    add_compile_options("$<$<COMPILE_LANGUAGE:C>:-Wno-error=implicit-function-declaration;-Wno-error=incompatible-pointer-types;-Wno-error=int-conversion>")
endif()

# ---------------------------------------------------------------- dependencies
find_package(SDL2 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(PNG REQUIRED)
find_package(Ogg QUIET)
find_package(Vorbis QUIET)
if(NOT TARGET Vorbis::vorbisfile)
    include(FetchContent)
    FetchContent_Declare(ogg GIT_REPOSITORY https://github.com/xiph/ogg.git GIT_TAG v1.3.6 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(ogg)
    FetchContent_Declare(vorbis GIT_REPOSITORY https://github.com/xiph/vorbis.git GIT_TAG v1.3.7 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(vorbis)
endif()
if(NG_SOUND)
    find_package(SDL2_mixer QUIET)
    if(NOT TARGET SDL2_mixer::SDL2_mixer AND NOT TARGET SDL2_mixer::SDL2_mixer-static)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDLMIX REQUIRED IMPORTED_TARGET SDL2_mixer)
        set(NG_MIXER PkgConfig::SDLMIX)
    elseif(TARGET SDL2_mixer::SDL2_mixer-static)
        set(NG_MIXER SDL2_mixer::SDL2_mixer-static)
    else()
        set(NG_MIXER SDL2_mixer::SDL2_mixer)
    endif()
endif()

# ---------------------------------------------------------------- sources
set(GAME_SOURCES
    ${SRC}/ccmx.c ${SRC}/console.c ${SRC}/lithium.c ${SRC}/lt_data.c ${SRC}/lt_fx.c
    ${SRC}/lt_game.c ${SRC}/lt_game2.c ${SRC}/lt_info.c ${SRC}/lt_input.c ${SRC}/lt_menu.c
    ${SRC}/lt_reso.c ${SRC}/lt_save.c ${SRC}/lt_sound.c ${SRC}/lt_st2.c ${SRC}/lt_start.c
    ${SRC}/lt_video.c ${SRC}/lt_weap.c)

set(ENGINE_SOURCES
    ${RLX}/src/gui_os.c ${RLX}/src/gx_csp.c ${RLX}/src/gx_flc.c ${RLX}/src/gx_form.c
    ${RLX}/src/gx_init.c ${RLX}/src/gx_png.c ${RLX}/src/gx_rgb.c ${RLX}/src/gx_tool.c
    ${RLX}/src/iss_av.c ${RLX}/src/iss_cdc.c ${RLX}/src/iss_db.c ${RLX}/src/iss_form.c
    ${RLX}/src/iss_ogg.c ${RLX}/src/sysgi.c ${RLX}/src/sysini.c ${RLX}/src/syslist.c
    ${RLX}/src/sysresmx.c ${RLX}/src/systools.c ${RLX}/src/syszlib.c ${RLX}/src/v3xcoll.c
    ${RLX}/src/v3xdata.c ${RLX}/src/v3xmaps.c ${RLX}/src/v3xrend.c ${RLX}/src/v3xscen2.c
    ${RLX}/src/v3xscene.c ${RLX}/src/v3xsoft.c ${RLX}/src/v3x_1.c ${RLX}/src/v3x_2.c
    ${RLX}/src/v3x_3.c ${RLX}/src/_stub.cpp)

set(PLATFORM_SOURCES
    ${RLX}/src/linux/systool2.c ${RLX}/src/linux/systime.c ${RLX}/src/linux/snd.c
    ${RLX}/src/linux/sysdbg.c ${RLX}/src/linux/_stublinux.cpp ${RLX}/src/linux/sysnet.c
    ${RLX}/src/linux/sysreg.c ${RLX}/src/linux/sysjoy.c ${RLX}/src/linux/syskeyb.c
    ${RLX}/src/linux/sysmouse.c ${SRC}/Linux/preferences.c
    ${RLX}/src/renderers/opengl/gl_gx.cpp ${RLX}/src/renderers/opengl/gl_v3x.cpp
    ${RLX}/src/renderers/opengl/linux/sdl_gl.cpp
    ${PORT}/sdl2/ng_sdl2.c)
# snd.c #includes the driver (snd_sdlmixer.c or snd_none.c) chosen by HAVE_* defines.

set(NG_DEFS __LINUX__ LINUX USE_OPENGL NG_SDL2)
if(NG_SOUND)
    list(APPEND NG_DEFS SOUND_SDL_MIXER HAVE_SDL_SDL_MIXER_H HAVE_LIBSDL_MIXER)
endif()

set(NG_INCLUDES ${PORT}/compat ${PORT}/sdl2 ${PORT}/gen ${RLX}/include ${SRC} ${RLX}/src ${RLX}/src/linux
    ${RLX}/src/renderers/opengl ${RLX}/src/renderers/opengl/linux)

set(NG_LIBS SDL2::SDL2 ZLIB::ZLIB PNG::PNG Vorbis::vorbisfile Vorbis::vorbis Ogg::ogg ${NG_MIXER})

if(NG_GLES)
    list(APPEND PLATFORM_SOURCES ${PORT}/gles/glcompat.cpp)
    list(APPEND NG_DEFS NG_GLES)
    list(APPEND NG_INCLUDES ${PORT}/gles)
endif()

if(ANDROID)
    file(GLOB QUEST_SOURCES CONFIGURE_DEPENDS ${PORT}/quest/src/*.cpp ${PORT}/quest/src/*.c)
    add_library(main SHARED ${GAME_SOURCES} ${ENGINE_SOURCES} ${PLATFORM_SOURCES} ${QUEST_SOURCES})
    set(NG_TARGET main)
    list(APPEND NG_DEFS NG_QUEST)
    list(APPEND NG_INCLUDES ${PORT}/quest/src)
else()
    add_executable(nogravity ${GAME_SOURCES} ${ENGINE_SOURCES} ${PLATFORM_SOURCES})
    set(NG_TARGET nogravity)
    if(NG_GLES)
        list(APPEND NG_LIBS GLESv2 EGL)
    else()
        find_package(OpenGL REQUIRED)
        list(APPEND NG_LIBS OpenGL::GL)
    endif()
endif()

target_compile_definitions(${NG_TARGET} PRIVATE ${NG_DEFS})
target_include_directories(${NG_TARGET} PRIVATE ${NG_INCLUDES})
target_link_libraries(${NG_TARGET} PRIVATE ${NG_LIBS} m)
