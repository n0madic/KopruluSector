# Spike 16: the SDL3 runtime's base checks (docs/spikes/16-sdl3.md).
# Configure through spikes/16-sdl3/build.sh, which passes the platform's toolchain.
cmake_minimum_required(VERSION 3.24)
project(koprulu_spike_sdl3 C)

set(KP_TOOLCHAIN "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/toolchain" CACHE PATH "tools/toolchain")
set(KP_SDL_DIR "${KP_TOOLCHAIN}/sdl/SDL3-3.4.18")
set(KP_SDL_MIXER_DIR "${KP_TOOLCHAIN}/sdl/SDL3_mixer-3.2.4")
set(KP_SPIKE_OGG "" CACHE FILEPATH "an OGG from generated/sounds")

# SDL3 as a static subproject; on Android a shared libSDL3.so, which SDLActivity loads
# before libmain.so.
if(ANDROID)
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  set(KP_SDL_TARGET SDL3::SDL3-shared)
else()
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(KP_SDL_TARGET SDL3::SDL3-static)
endif()
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory("${KP_SDL_DIR}" sdl EXCLUDE_FROM_ALL)

# SDL3_mixer with the two decoders the game uses: WAV (the original's files) and Ogg Vorbis
# through the bundled stb_vorbis; nothing that needs another library.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDLMIXER_VENDORED OFF CACHE BOOL "" FORCE)
set(SDLMIXER_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLMIXER_TESTS OFF CACHE BOOL "" FORCE)
foreach(off AIFF VOC AU FLAC GME MOD MP3 MIDI OPUS VORBIS_VORBISFILE VORBIS_TREMOR WAVPACK)
  set(SDLMIXER_${off} OFF CACHE BOOL "" FORCE)
endforeach()
set(SDLMIXER_WAVE ON CACHE BOOL "" FORCE)
set(SDLMIXER_VORBIS_STB ON CACHE BOOL "" FORCE)
add_subdirectory("${KP_SDL_MIXER_DIR}" sdl_mixer EXCLUDE_FROM_ALL)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/assets.h"
  COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/make_assets.py" "${KP_SPIKE_OGG}"
          "${CMAKE_CURRENT_BINARY_DIR}/assets.h"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/make_assets.py" "${KP_SPIKE_OGG}")

if(ANDROID)
  # SDL's Java activity loads libmain.so.
  add_library(main SHARED spike.c "${CMAKE_CURRENT_BINARY_DIR}/assets.h")
  set(target main)
else()
  add_executable(spike_sdl3 spike.c "${CMAKE_CURRENT_BINARY_DIR}/assets.h")
  set(target spike_sdl3)
endif()
target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(${target} PRIVATE SDL3_mixer::SDL3_mixer-static ${KP_SDL_TARGET})
set_property(TARGET ${target} PROPERTY C_STANDARD 99)

if(EMSCRIPTEN)
  set_target_properties(${target} PROPERTIES SUFFIX ".html")
  target_link_options(${target} PRIVATE -sUSE_WEBGL2=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
    -sFULL_ES3=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -lidbfs.js "-sEXPORTED_RUNTIME_METHODS=['FS']")
elseif(ANDROID)
  target_link_libraries(${target} PRIVATE GLESv3 log android)
elseif(IOS)
  target_link_libraries(${target} PRIVATE "-framework OpenGLES")
  set_target_properties(${target} PROPERTIES MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER com.koprulusector.spike
    MACOSX_BUNDLE_BUNDLE_NAME spike_sdl3
    MACOSX_BUNDLE_SHORT_VERSION_STRING 0.1 MACOSX_BUNDLE_BUNDLE_VERSION 1)
elseif(APPLE)
  target_link_libraries(${target} PRIVATE "-framework OpenGL")
elseif(UNIX)
  target_link_libraries(${target} PRIVATE GL)
endif()
