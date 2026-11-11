# libogg and libvorbis with its encoder (BSD) as one library, kp_vorbis: the Ogg Vorbis encoder of the host's kp_convert
# (ADR 0019: the repack's sounds and speech, the 1.17 cinematics' sound; the runtime decodes Ogg Vorbis with
# SDL3_mixer's stb_vorbis and needs none of this), from the pinned releases `tools/toolchain/fetch.sh vorbis` fetches.
# libogg's config_types.h, which its configure writes, is generated/ogg/config_types.h. libvorbis's lib/CMakeLists.txt
# names the sources: the codec's and vorbisenc.c (not vorbisfile.c, the reader, nor the tools barkmel.c, psytune.c and
# tone.c). Included (include()d, not a subdirectory) by the top CMakeLists.txt where the tools are built; built without
# the project's warnings, like kp_vpx.
set(KP_OGG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tools/toolchain/vorbis/libogg-1.3.6)
set(KP_VORBIS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tools/toolchain/vorbis/libvorbis-1.3.7)
foreach(probe ${KP_OGG_DIR}/src/framing.c ${KP_VORBIS_DIR}/lib/vorbisenc.c)
	if(NOT EXISTS ${probe})
		message(FATAL_ERROR "the Vorbis encoder needs its sources: run tools/toolchain/fetch.sh vorbis")
	endif()
endforeach()

set(KP_VORBIS_SOURCES
	${KP_OGG_DIR}/src/bitwise.c ${KP_OGG_DIR}/src/framing.c)
foreach(source mdct smallft block envelope window lsp lpc analysis synthesis psy info floor1 floor0 res0 mapping0
		registry codebook sharedbook lookup bitrate vorbisenc)
	list(APPEND KP_VORBIS_SOURCES ${KP_VORBIS_DIR}/lib/${source}.c)
endforeach()
add_library(kp_vorbis STATIC ${KP_VORBIS_SOURCES})
target_include_directories(kp_vorbis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vorbis/generated
	${KP_OGG_DIR}/include ${KP_VORBIS_DIR}/include)
target_include_directories(kp_vorbis PRIVATE ${KP_VORBIS_DIR}/lib)
set_target_properties(kp_vorbis PROPERTIES POSITION_INDEPENDENT_CODE ON)
# alloca: libvorbis's os.h includes alloca.h where configure found it (glibc declares it nowhere else in C99), malloc.h
# on Windows
include(CheckIncludeFile)
check_include_file(alloca.h KP_VORBIS_ALLOCA_H)
if(KP_VORBIS_ALLOCA_H)
	target_compile_definitions(kp_vorbis PRIVATE HAVE_ALLOCA_H)
endif()
# libvorbis shifts negative values left (psy.c's bark table: (lo - 1) << 16 with lo 0), which UBSan stops on: its
# sources run without that one check, the rest of UBSan and ASan kept
if(KP_SANITIZE)
	target_compile_options(kp_vorbis PRIVATE -fno-sanitize=shift-base)
endif()
if(MSVC)
	target_compile_definitions(kp_vorbis PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
	target_compile_options(kp_vorbis PRIVATE -w)
	target_link_libraries(kp_vorbis PRIVATE m)
endif()
