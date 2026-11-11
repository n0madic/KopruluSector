# The native build (plan phase S, ADR 0012): the simulation core kp_core (C99 in the common subset of C and C++,
# ADR 0008), the presentation kp_present, the headless application kp_app_core, the command-line tools (kp_replay,
# kp_bench, kp_load_check, kp_oracle), the C test runner kp_test (tests/c) and, with KP_APP, the product (kp_app over
# SDL 3.4 with kp_gfx; tools/product.sh builds it for each platform).
#
#   cmake -S . -B build/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/cmake
#   cmake -S . -B build/cmake-asan -G Ninja -DKP_SANITIZE=ON && cmake --build build/cmake-asan
#   emcmake cmake -S . -B build/cmake-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/cmake-wasm
#     (kp_test.js and kp_oracle.js under node, the host's files through NODERAWFS)
cmake_minimum_required(VERSION 3.20)
project(KopruluSector C CXX)

option(KP_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(KP_CXX_CHECK "Check the C sources as C++11 (they stay in the common subset of C and C++, ADR 0008)" ON)
option(KP_APP "Build the product: kp_app over SDL 3.4 and SDL3_mixer, kp_gfx (plan phase S, stage S5)" OFF)
option(KP_TOOLS "Build the tools and the C suites" ON)
option(KP_ASTC "Build kp_hd_phone's ASTC modes over the Arm ASTC encoder (tools/toolchain/fetch.sh astc; ADR 0013)" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(KP_WARNINGS -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion -Werror)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
	# GCC's guesses about snprintf truncation (the messages are cut on purpose) and its stricter float conversions
	list(APPEND KP_WARNINGS -Wno-format-truncation)
endif()
# The project's warnings on a target; CXX: its sources join the C++11 check (KP_CXX_CHECK) too.
function(kp_target name)
	target_compile_options(${name} PRIVATE ${KP_WARNINGS})
	if("CXX" IN_LIST ARGN)
		get_target_property(sources ${name} SOURCES)
		set_property(GLOBAL APPEND PROPERTY KP_CXX_SOURCES ${sources})
	endif()
endfunction()

set(KP_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/native/kp_core)
set(KP_TOOLS_DIR ${KP_CORE_DIR}/tools)
set(KP_TESTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/c)

if(KP_SANITIZE)
	set(KP_SANITIZE_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
	# C and C++ only: kp_vpx's NASM sources (x86-64) take none of them
	set(KP_SANITIZE_COMPILE ${KP_SANITIZE_FLAGS} -g)
	if(CMAKE_BUILD_TYPE STREQUAL "Release")
		list(APPEND KP_SANITIZE_COMPILE -O1)
	endif()
	add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:${KP_SANITIZE_COMPILE}>")
	add_link_options(${KP_SANITIZE_FLAGS})
endif()

# One wasm stack for the suites under node and the product's page: the suites prove the core on the stack the page
# gives it (a wasm stack overflow corrupts memory silently).
set(KP_WASM_STACK -sSTACK_SIZE=8MB)

if(EMSCRIPTEN AND KP_APP)
	set(KP_TOOLS OFF) # the product's page; the node build of the tools is its own configuration
endif()
if(EMSCRIPTEN AND KP_TOOLS)
	set(KP_CXX_CHECK OFF)
	# Node with the host's file system (the repository's generated data and goldens), a heap that grows with the games.
	set(KP_NODE_LINK -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 ${KP_WASM_STACK} -sEXIT_RUNTIME=1 -sENVIRONMENT=node)
	add_link_options(${KP_NODE_LINK})
endif()

# --- kp_core: the simulation and the interface rules ---
# src/defs: the tables of defs/ in C (tools/defs_c.py, ADR 0018)
file(GLOB KP_CORE_SOURCES CONFIGURE_DEPENDS ${KP_CORE_DIR}/src/core/*.c ${KP_CORE_DIR}/src/defs/*.c)
add_library(kp_core STATIC ${KP_CORE_SOURCES})
kp_target(kp_core CXX)
target_include_directories(kp_core PUBLIC ${KP_CORE_DIR}/src/core ${KP_CORE_DIR}/src/defs)
if(NOT WIN32)
	target_link_libraries(kp_core PUBLIC m)
endif()

# --- kp_present: the presentation in plain C (no SDL, no GL; plan phase S, stage S2) ---
set(KP_PRESENT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/native/kp_present)
# stb_truetype (third_party/README.md) for the vector text: its own library, without the project's warnings
add_library(kp_stb STATIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb/kp_stb_truetype.c)
target_include_directories(kp_stb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
if(NOT MSVC)
	target_compile_options(kp_stb PRIVATE -w)
endif()
# libvpx's VP8 and VP9 decoders and nestegg (tools/toolchain/fetch.sh vpx): the cinematics (ADR 0015), Remastered's WebM
# (ADR 0018)
include(${CMAKE_CURRENT_SOURCE_DIR}/third_party/vpx/kp_vpx.cmake)
# The readers of the original data (ADR 0018): StormLib, CascLib, zlib, bzip2 (tools/toolchain/fetch.sh data), and
# kp_casc_c, CascLib for C (native/kp_app/kp_casc.cpp)
include(${CMAKE_CURRENT_SOURCE_DIR}/third_party/data/kp_data.cmake)
add_library(kp_casc_c STATIC ${CMAKE_CURRENT_SOURCE_DIR}/native/kp_app/kp_casc.cpp)
kp_target(kp_casc_c)
target_include_directories(kp_casc_c PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/native/kp_app)
target_link_libraries(kp_casc_c PRIVATE kp_casc)
file(GLOB KP_PRESENT_SOURCES CONFIGURE_DEPENDS ${KP_PRESENT_DIR}/*.c)
add_library(kp_present STATIC ${KP_PRESENT_SOURCES})
kp_target(kp_present CXX)
target_include_directories(kp_present PUBLIC ${KP_PRESENT_DIR})
target_link_libraries(kp_present PUBLIC kp_core PRIVATE kp_stb)

# --- kp_gfx_passes: kp_gfx's passes and shaders over its narrow backend (kp_gfx_internal.h; no SDL, no GL): kp_gfx
# implements the backend over GL, the C suites record it ---
set(KP_GFX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/native/kp_gfx)
add_library(kp_gfx_passes STATIC ${KP_GFX_DIR}/kp_gfx_passes.c ${KP_GFX_DIR}/kp_gfx_shaders.c)
kp_target(kp_gfx_passes)
target_include_directories(kp_gfx_passes PUBLIC ${KP_GFX_DIR})
target_link_libraries(kp_gfx_passes PUBLIC kp_present)

# --- kp_app_core: the product's headless part (the session, the saves, the profile, the interface's host; S2) ---
set(KP_APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/native/kp_app)
file(GLOB KP_APP_CORE_SOURCES CONFIGURE_DEPENDS ${KP_APP_DIR}/*.c)
add_library(kp_app_core STATIC ${KP_APP_CORE_SOURCES})
kp_target(kp_app_core CXX)
target_include_directories(kp_app_core PUBLIC ${KP_APP_DIR})
# kp_res (ADR 0018); the video decoders (kp_cine.c, kp_webm.c)
target_link_libraries(kp_app_core PUBLIC kp_present kp_storm kp_casc_c kp_vpx kp_nestegg kp_zlib) # zlib: kp_res_scr's SBIN

if(KP_TOOLS)
# --- the oracle library: replays, goldens, the native games of the oracles, the campaign chains, the interface's
# replays (native/kp_core/tools/kp_runner.h, kp_oracle.h) ---
set(KP_ORACLE_SOURCES
	${KP_TOOLS_DIR}/kp_runner.c
	${KP_TOOLS_DIR}/kp_json.c
	${KP_TOOLS_DIR}/kp_oracle.c
	${KP_TOOLS_DIR}/kp_oracle_ui.c)
add_library(kp_oracle_lib STATIC ${KP_ORACLE_SOURCES})
kp_target(kp_oracle_lib CXX)
target_include_directories(kp_oracle_lib PUBLIC ${KP_TOOLS_DIR})
target_link_libraries(kp_oracle_lib PUBLIC kp_core kp_present)

# --- the tools ---
foreach(tool kp_replay kp_bench)
	add_executable(${tool} ${KP_TOOLS_DIR}/${tool}.c)
	kp_target(${tool} CXX)
	target_link_libraries(${tool} PRIVATE kp_oracle_lib)
endforeach()
add_executable(kp_hd_phone ${KP_TOOLS_DIR}/kp_hd_phone.c)
kp_target(kp_hd_phone CXX)
target_link_libraries(kp_hd_phone PRIVATE kp_present)
if(KP_ASTC)
	include(${CMAKE_CURRENT_SOURCE_DIR}/third_party/astcenc/kp_astcenc.cmake)
	target_compile_definitions(kp_hd_phone PRIVATE KP_ASTC=1)
	target_include_directories(kp_hd_phone SYSTEM PRIVATE ${KP_ASTC_DIR})
	find_package(Threads REQUIRED)
	target_link_libraries(kp_hd_phone PRIVATE kp_astcenc Threads::Threads)
	set(KP_CXX_ASTC_INCLUDE -DKP_ASTC=1 -isystem ${KP_ASTC_DIR}) # the C++ check sees the tool's ASTC half too
	set_target_properties(kp_hd_phone PROPERTIES LINKER_LANGUAGE CXX)
endif()
add_executable(kp_fnt_make ${KP_TOOLS_DIR}/kp_fnt_make.c)
kp_target(kp_fnt_make CXX)
target_link_libraries(kp_fnt_make PRIVATE kp_present)
add_executable(kp_load_check ${KP_TOOLS_DIR}/kp_load_check.c)
kp_target(kp_load_check CXX)
target_link_libraries(kp_load_check PRIVATE kp_core)
add_executable(kp_oracle ${KP_TOOLS_DIR}/kp_oracle_main.c ${KP_TOOLS_DIR}/kp_oracle_cli_ui.c)
kp_target(kp_oracle CXX)
target_link_libraries(kp_oracle PRIVATE kp_oracle_lib)
add_executable(kp_data_probe ${KP_TOOLS_DIR}/kp_data_probe.c)
kp_target(kp_data_probe CXX)
target_link_libraries(kp_data_probe PRIVATE kp_app_core)
set_target_properties(kp_data_probe PROPERTIES LINKER_LANGUAGE CXX)

# --- kp_test: the C suites (tests/c) ---
file(GLOB KP_TEST_SOURCES CONFIGURE_DEPENDS ${KP_TESTS_DIR}/*.c)
add_executable(kp_test ${KP_TEST_SOURCES})
kp_target(kp_test CXX)
target_include_directories(kp_test PRIVATE ${KP_TESTS_DIR})
target_link_libraries(kp_test PRIVATE kp_oracle_lib kp_app_core kp_gfx_passes) # kp_app_core's zlib: test_original's PNGs too

endif()

# --- the C++11 check of everything above (a stamp per run of the check, redone when a source changes) ---
if(KP_CXX_CHECK AND KP_TOOLS)
	file(GLOB KP_CXX_HEADERS CONFIGURE_DEPENDS ${KP_CORE_DIR}/src/core/*.h ${KP_CORE_DIR}/src/defs/*.h ${KP_PRESENT_DIR}/*.h ${KP_APP_DIR}/*.h ${KP_TOOLS_DIR}/*.h
		${KP_TESTS_DIR}/*.h ${KP_GFX_DIR}/kp_gfx.h ${KP_GFX_DIR}/kp_gfx_internal.h)
	get_property(KP_CXX_SOURCES GLOBAL PROPERTY KP_CXX_SOURCES)
	set(KP_CXX_STAMP ${CMAKE_BINARY_DIR}/kp_cxx_check.stamp)
	set(KP_CXX_HOST_DEFS "")
	if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		# libstdc++'s <cstdlib> wants quick_exit, which macOS's <stdlib.h> hides from a file that defines _POSIX_C_SOURCE
		set(KP_CXX_HOST_DEFS -D_DARWIN_C_SOURCE=) # empty, as the files that define it themselves
	endif()
	add_custom_command(OUTPUT ${KP_CXX_STAMP}
		COMMAND ${CMAKE_CXX_COMPILER} -std=c++11 -fsyntax-only -x c++ ${KP_WARNINGS} -DKP_CPP_CHECK ${KP_CXX_HOST_DEFS}
			-I${KP_CORE_DIR}/src/core -I${KP_CORE_DIR}/src/defs -I${KP_PRESENT_DIR} -I${KP_APP_DIR} -I${KP_TOOLS_DIR} -I${KP_TESTS_DIR} -I${KP_GFX_DIR}
			-I${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb -isystem ${KP_STORM_DIR}/src -isystem ${KP_VPX_DIR} -isystem ${KP_NESTEGG_DIR}/include ${KP_CXX_ASTC_INCLUDE} ${KP_CXX_SOURCES}
		COMMAND ${CMAKE_COMMAND} -E touch ${KP_CXX_STAMP}
		DEPENDS ${KP_CXX_SOURCES} ${KP_CXX_HEADERS}
		COMMENT "Checking the C sources as C++11"
		VERBATIM)
	add_custom_target(kp_cxx_check ALL DEPENDS ${KP_CXX_STAMP})
endif()

# --- the product: kp_gfx and kp_app over SDL 3.4.18 and SDL3_mixer 3.2.4 (tools/toolchain/fetch.sh) ---
if(KP_APP)
	set(KP_SDL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tools/toolchain/sdl/SDL3-3.4.18)
	set(KP_SDL_MIXER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tools/toolchain/sdl/SDL3_mixer-3.2.4)
	# SDL3 static; on Android a shared libSDL3.so, which SDLActivity loads before libmain.so
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
	if(EMSCRIPTEN)
		set(CMAKE_C_EXTENSIONS ON) # EM_ASM needs gnu99 (SDL's sources and the product's)
	endif()
	add_subdirectory(${KP_SDL_DIR} sdl EXCLUDE_FROM_ALL)
	# SDL3_mixer with the two decoders the game uses: WAV and Ogg Vorbis through the bundled stb_vorbis
	set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
	set(SDLMIXER_VENDORED OFF CACHE BOOL "" FORCE)
	set(SDLMIXER_SAMPLES OFF CACHE BOOL "" FORCE)
	set(SDLMIXER_TESTS OFF CACHE BOOL "" FORCE)
	foreach(off AIFF VOC AU FLAC GME MOD MP3 MIDI OPUS VORBIS_VORBISFILE VORBIS_TREMOR WAVPACK)
		set(SDLMIXER_${off} OFF CACHE BOOL "" FORCE)
	endforeach()
	set(SDLMIXER_WAVE ON CACHE BOOL "" FORCE)
	set(SDLMIXER_VORBIS_STB ON CACHE BOOL "" FORCE)
	add_subdirectory(${KP_SDL_MIXER_DIR} sdl_mixer EXCLUDE_FROM_ALL)

	add_library(kp_gfx STATIC ${KP_GFX_DIR}/kp_gfx_gl.c ${KP_GFX_DIR}/kp_gl.c)
	kp_target(kp_gfx)
	target_link_libraries(kp_gfx PUBLIC kp_gfx_passes ${KP_SDL_TARGET})

	file(GLOB KP_APP_SOURCES CONFIGURE_DEPENDS ${KP_APP_DIR}/sdl/*.c)
	if(ANDROID)
		add_library(main SHARED ${KP_APP_SOURCES}) # SDL's Java activity loads libmain.so
		set(KP_APP_TARGET main)
	else()
		set(KP_APP_RESOURCE_FILES "")
		if(WIN32 AND KP_APP_ICON) # the icon (converter's generated/icon/icon.ico) as the executable's resource 1, which SDL gives its window class
			enable_language(RC)
			file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/koprulu.rc "1 ICON \"${KP_APP_ICON}\"\n")
			set(KP_APP_RESOURCE_FILES ${CMAKE_CURRENT_BINARY_DIR}/koprulu.rc)
			set_source_files_properties(${KP_APP_RESOURCE_FILES} PROPERTIES OBJECT_DEPENDS ${KP_APP_ICON})
		endif()
		add_executable(koprulu ${KP_APP_SOURCES} ${KP_APP_RESOURCE_FILES})
		set(KP_APP_TARGET koprulu)
	endif()
	kp_target(${KP_APP_TARGET})
	target_include_directories(${KP_APP_TARGET} PRIVATE ${KP_APP_DIR}/sdl)
	# the application's version and identifier (SDL's metadata, the iOS bundle's)
	set(KP_APP_VERSION 0.1)
	set(KP_APP_ID com.koprulusector.game)
	target_compile_definitions(${KP_APP_TARGET} PRIVATE KP_APP_VERSION="${KP_APP_VERSION}" KP_APP_ID="${KP_APP_ID}")
	target_link_libraries(${KP_APP_TARGET} PRIVATE kp_app_core kp_gfx SDL3_mixer::SDL3_mixer-static ${KP_SDL_TARGET})
	if(EMSCRIPTEN)
		set_target_properties(koprulu PROPERTIES SUFFIX ".html" LINK_DEPENDS ${KP_APP_DIR}/web/shell.html)
		target_link_options(koprulu PRIVATE -sUSE_WEBGL2=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1
			-sALLOW_MEMORY_GROWTH=1 ${KP_WASM_STACK} -sINITIAL_MEMORY=268435456 -lidbfs.js -sFETCH=1
			"-sEXPORTED_RUNTIME_METHODS=['FS']" --shell-file ${KP_APP_DIR}/web/shell.html ${KP_WEB_LINK})
	elseif(ANDROID)
		target_link_libraries(main PRIVATE GLESv3 log android)
	elseif(IOS)
		target_link_libraries(koprulu PRIVATE "-framework OpenGLES")
		if(KP_APP_RESOURCES) # the staged data (tools/product.sh ios) as the bundle's data/
			add_custom_command(TARGET koprulu POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${KP_APP_RESOURCES}
				$<TARGET_BUNDLE_CONTENT_DIR:koprulu>/data)
		endif()
		if(KP_APP_ICONS) # the loose icons (converter's generated/icon/ios) which Info.plist's CFBundleIcons names
			add_custom_command(TARGET koprulu POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${KP_APP_ICONS}
				$<TARGET_BUNDLE_CONTENT_DIR:koprulu>)
		endif()
		set_target_properties(koprulu PROPERTIES MACOSX_BUNDLE TRUE MACOSX_BUNDLE_GUI_IDENTIFIER ${KP_APP_ID}
			MACOSX_BUNDLE_BUNDLE_NAME "Koprulu Sector" MACOSX_BUNDLE_SHORT_VERSION_STRING ${KP_APP_VERSION} MACOSX_BUNDLE_BUNDLE_VERSION 1
			MACOSX_BUNDLE_INFO_PLIST ${KP_APP_DIR}/ios/Info.plist.in)
	elseif(APPLE)
		target_link_libraries(koprulu PRIVATE "-framework OpenGL")
	elseif(WIN32)
		target_link_libraries(koprulu PRIVATE opengl32)
	elseif(UNIX)
		target_link_libraries(koprulu PRIVATE GL)
	endif()
endif()
