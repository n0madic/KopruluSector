# libvpx's VP8 and VP9 decoders as a library of their own (ADR 0015, ADR 0018), with VP9's encoder too in a build of
# the host's tools (KP_ENCODERS: kp_convert's cinematics, ADR 0019), and nestegg, the WebM demuxer, as another: the
# sources of the pinned libvpx release and nestegg commit fetched by `tools/toolchain/fetch.sh vpx`. libvpx without
# threads or high bit depth (C11, libvpx 1.17 uses static_assert), with the headers configure generates and the list of
# sources it builds kept in generated/<target>[-enc]/ (regenerate.sh): on arm64 (macOS, iOS, Android, Linux) its NEON
# intrinsics (generated/arm64/, NEON is part of every AArch64 CPU; no dotprod, i8mm or SVE, which would need a runtime
# check); on x86-64 (Windows, Linux, an Intel Mac) with NASM 2.14 or later on the PATH its MMX..AVX512 chosen at run
# time by the CPU's cpuid (generated/x86_64/: intrinsics and libvpx's .asm, which part of the functions the headers name
# are written in only; each intrinsics file built for its extension, as libvpx's build/make/Makefile does by its name's
# suffix); elsewhere (the web's wasm), and on x86-64 without NASM, its C (generated/generic/, the generic-gnu target).
# VP9 plays the cinematics' and Remastered's VP9 WebM, VP8 Remastered's portraits and glue animations (kp_webm.c).
# Included (include()d, not a subdirectory) by the top CMakeLists.txt. Built without the project's warnings, like
# kp_stb.
set(KP_VPX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tools/toolchain/vpx/libvpx-1.17.0)
set(KP_NESTEGG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tools/toolchain/vpx/nestegg-767aab25013acefbdcc6d68a2a7a2c9081303e3f)
foreach(probe ${KP_VPX_DIR}/vpx/vpx_decoder.h ${KP_NESTEGG_DIR}/src/nestegg.c)
	if(NOT EXISTS ${probe})
		message(FATAL_ERROR "the video decoders need their sources: run tools/toolchain/fetch.sh vpx")
	endif()
endforeach()

# NEON when the compiler targets AArch64 alone, SSE2..AVX512 when it targets x86-64 alone and NASM is there (asked of the
# compiler, not of CMake's processor variables, which can name the host; a macOS build of several architectures takes
# the C), checked again at each configure
include(CheckCSourceCompiles)
unset(KP_VPX_AARCH64 CACHE)
unset(KP_VPX_X86_64 CACHE)
set(CMAKE_REQUIRED_QUIET ON)
check_c_source_compiles("#if !defined(__aarch64__) && !defined(_M_ARM64)
#error not AArch64
#endif
int main(void) { return 0; }" KP_VPX_AARCH64)
check_c_source_compiles("#if !defined(__x86_64__) && !defined(_M_X64) || defined(_M_ARM64EC)
#error not x86-64
#endif
int main(void) { return 0; }" KP_VPX_X86_64)
unset(CMAKE_REQUIRED_QUIET)
set(KP_VPX_NASM_MIN 2.14) # libvpx's README: x86inc.asm needs it
set(KP_VPX_NASM OFF)
if(KP_VPX_X86_64)
	include(CheckLanguage)
	if(NOT CMAKE_ASM_NASM_COMPILER)
		unset(CMAKE_ASM_NASM_COMPILER CACHE) # a NASM installed since the last configure
	endif()
	check_language(ASM_NASM)
	if(CMAKE_ASM_NASM_COMPILER)
		execute_process(COMMAND ${CMAKE_ASM_NASM_COMPILER} -v OUTPUT_VARIABLE KP_VPX_NASM_OUT ERROR_QUIET)
		string(REGEX MATCH "version ([0-9]+\\.[0-9]+)" KP_VPX_NASM_VERSION "${KP_VPX_NASM_OUT}")
		if(CMAKE_MATCH_1 VERSION_GREATER_EQUAL KP_VPX_NASM_MIN)
			set(KP_VPX_NASM ON)
		else()
			message(STATUS "libvpx: NASM ${CMAKE_MATCH_1} is older than ${KP_VPX_NASM_MIN}: x86-64 decodes from C")
		endif()
	else()
		message(STATUS "libvpx: no NASM on the PATH: x86-64 decodes from C")
	endif()
endif()
if(KP_VPX_AARCH64)
	set(KP_VPX_ARCH arm64)
elseif(KP_VPX_NASM)
	enable_language(ASM_NASM)
	set(KP_VPX_ARCH x86_64)
else()
	set(KP_VPX_ARCH generic)
endif()
set(KP_VPX_CONFIG ${KP_VPX_ARCH})
if(KP_ENCODERS)
	set(KP_VPX_CONFIG ${KP_VPX_ARCH}-enc)
endif()
message(STATUS "libvpx: the ${KP_VPX_CONFIG} build")
set(KP_VPX_GENERATED ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vpx/generated)
file(STRINGS ${KP_VPX_GENERATED}/${KP_VPX_CONFIG}/sources.txt KP_VPX_SOURCES)
list(TRANSFORM KP_VPX_SOURCES PREPEND ${KP_VPX_DIR}/)
if(KP_VPX_ARCH STREQUAL x86_64)
	# an intrinsics file built for the extension its name ends with, called only when cpuid reports it (vpx_ports/x86.h)
	if(MSVC)
		set(KP_VPX_EXT_avx /arch:AVX)
		set(KP_VPX_EXT_avx2 /arch:AVX2)
		set(KP_VPX_EXT_avx512 /arch:AVX512) # MMX..SSE4.1 need no flag of MSVC's
	else()
		set(KP_VPX_EXT_mmx -mmmx)
		set(KP_VPX_EXT_sse2 -msse2)
		set(KP_VPX_EXT_sse3 -msse3)
		set(KP_VPX_EXT_ssse3 -mssse3)
		set(KP_VPX_EXT_sse4 -msse4.1)
		set(KP_VPX_EXT_avx -mavx)
		set(KP_VPX_EXT_avx2 -mavx2)
		set(KP_VPX_EXT_avx512 -mavx512f -mavx512cd -mavx512bw -mavx512dq -mavx512vl)
	endif()
	foreach(source ${KP_VPX_SOURCES})
		if(source MATCHES "_(mmx|sse2|sse3|ssse3|sse4|avx|avx2|avx512)\\.c$") # CMAKE_MATCH_1 is set once this ran
			set(ext ${CMAKE_MATCH_1})
			if(DEFINED KP_VPX_EXT_${ext})
				set_source_files_properties(${source} PROPERTIES COMPILE_OPTIONS "${KP_VPX_EXT_${ext}}")
			endif()
		endif()
	endforeach()
endif()

add_library(kp_vpx STATIC ${KP_VPX_SOURCES} ${KP_VPX_GENERATED}/${KP_VPX_CONFIG}/vpx_config.c)
# generated/ first: its vpx_config.h, vpx_version.h and *_rtcd.h stand in for the ones configure writes (and for the
# .asm its vpx_config.asm, beside the source's third_party/x86inc/x86inc.asm)
target_include_directories(kp_vpx PRIVATE ${KP_VPX_GENERATED}/${KP_VPX_CONFIG} ${KP_VPX_GENERATED} ${KP_VPX_DIR})
target_include_directories(kp_vpx PUBLIC ${KP_VPX_DIR})
set_target_properties(kp_vpx PROPERTIES C_STANDARD 11 C_EXTENSIONS ON POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
	target_compile_options(kp_vpx PRIVATE $<$<COMPILE_LANGUAGE:C>:-w>)
endif()

# nestegg (ISC): the WebM demuxer kp_webm.c reads Remastered's and the cinematics' WebM with
add_library(kp_nestegg STATIC ${KP_NESTEGG_DIR}/src/nestegg.c)
target_include_directories(kp_nestegg PUBLIC ${KP_NESTEGG_DIR}/include)
set_target_properties(kp_nestegg PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
	target_compile_options(kp_nestegg PRIVATE -w)
endif()
