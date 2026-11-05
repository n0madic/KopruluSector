# libvpx's VP9 decoder as a library of its own (ADR 0015): the sources of the pinned libvpx release fetched by
# `tools/toolchain/fetch.sh vpx`, without threads, VP8, the encoder or high bit depth (C11, libvpx 1.17 uses
# static_assert), with the few headers configure generates kept in generated/ (regenerate.sh): on arm64 (macOS,
# iOS, Android, Linux) its NEON intrinsics (generated/arm64/, NEON is part of every AArch64 CPU; no dotprod, i8mm or SVE,
# which would need a runtime check); on x86-64 (Windows, Linux, an Intel Mac) with NASM 2.14 or later on the PATH its
# SSE2..AVX2 chosen at run time by the CPU's cpuid (generated/x86_64/: intrinsics and libvpx's .asm, which part of the
# functions the headers name are written in only); elsewhere (the web's wasm), and on x86-64 without NASM, its C
# (generated/generic/, the generic-gnu target). Included (include()d, not a subdirectory) by the top CMakeLists.txt only with
# -DKP_CINEMATICS=ON. Built without the project's warnings, like kp_stb.
set(KP_VPX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tools/toolchain/vpx/libvpx-1.17.0)
if(NOT EXISTS ${KP_VPX_DIR}/vpx/vpx_decoder.h)
	message(FATAL_ERROR "KP_CINEMATICS needs the libvpx source: run tools/toolchain/fetch.sh vpx")
endif()

set(KP_VPX_SOURCES
	vp9/common/vp9_alloccommon.c vp9/common/vp9_blockd.c vp9/common/vp9_common_data.c vp9/common/vp9_entropy.c
	vp9/common/vp9_entropymode.c vp9/common/vp9_entropymv.c vp9/common/vp9_filter.c vp9/common/vp9_frame_buffers.c
	vp9/common/vp9_idct.c vp9/common/vp9_loopfilter.c vp9/common/vp9_mvref_common.c vp9/common/vp9_pred_common.c
	vp9/common/vp9_quant_common.c vp9/common/vp9_reconinter.c vp9/common/vp9_reconintra.c vp9/common/vp9_rtcd.c
	vp9/common/vp9_scale.c vp9/common/vp9_scan.c vp9/common/vp9_seg_common.c vp9/common/vp9_thread_common.c
	vp9/common/vp9_tile_common.c
	vp9/decoder/vp9_decodeframe.c vp9/decoder/vp9_decodemv.c vp9/decoder/vp9_decoder.c vp9/decoder/vp9_detokenize.c
	vp9/decoder/vp9_dsubexp.c vp9/decoder/vp9_job_queue.c
	vp9/vp9_dx_iface.c vp9/vp9_iface_common.c
	vpx/src/vpx_codec.c vpx/src/vpx_decoder.c vpx/src/vpx_image.c
	vpx_dsp/bitreader.c vpx_dsp/bitreader_buffer.c vpx_dsp/intrapred.c vpx_dsp/inv_txfm.c vpx_dsp/loopfilter.c
	vpx_dsp/prob.c vpx_dsp/vpx_convolve.c vpx_dsp/vpx_dsp_rtcd.c
	vpx_mem/vpx_mem.c
	vpx_scale/generic/gen_scalers.c vpx_scale/generic/vpx_scale.c vpx_scale/generic/yv12config.c
	vpx_scale/generic/yv12extend.c vpx_scale/vpx_scale_rtcd.c
	vpx_util/vpx_thread.c)

# NEON when the compiler targets AArch64 alone, SSE2..AVX2 when it targets x86-64 alone and NASM is there (asked of the
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
	list(APPEND KP_VPX_SOURCES
		vp9/common/arm/neon/vp9_iht16x16_add_neon.c vp9/common/arm/neon/vp9_iht4x4_add_neon.c
		vp9/common/arm/neon/vp9_iht8x8_add_neon.c
		vpx_dsp/arm/idct16x16_1_add_neon.c vpx_dsp/arm/idct16x16_add_neon.c vpx_dsp/arm/idct32x32_135_add_neon.c
		vpx_dsp/arm/idct32x32_1_add_neon.c vpx_dsp/arm/idct32x32_34_add_neon.c vpx_dsp/arm/idct32x32_add_neon.c
		vpx_dsp/arm/idct4x4_1_add_neon.c vpx_dsp/arm/idct4x4_add_neon.c vpx_dsp/arm/idct8x8_1_add_neon.c
		vpx_dsp/arm/idct8x8_add_neon.c vpx_dsp/arm/intrapred_neon.c vpx_dsp/arm/loopfilter_neon.c
		vpx_dsp/arm/vpx_convolve8_neon.c vpx_dsp/arm/vpx_convolve_avg_neon.c vpx_dsp/arm/vpx_convolve_copy_neon.c
		vpx_dsp/arm/vpx_convolve_neon.c vpx_dsp/arm/vpx_scaled_convolve8_neon.c
		vpx_ports/aarch64_cpudetect.c)
elseif(KP_VPX_NASM)
	enable_language(ASM_NASM)
	set(KP_VPX_ARCH x86_64)
	# the intrinsics, each file built for its own extension (libvpx's build/make/Makefile), called only when cpuid
	# reports it (vpx_ports/x86.h)
	set(KP_VPX_SSE2 vp9/common/x86/vp9_idct_intrin_sse2.c vpx_dsp/x86/inv_txfm_sse2.c vpx_dsp/x86/loopfilter_sse2.c
		vpx_dsp/x86/vpx_subpixel_4t_intrin_sse2.c)
	set(KP_VPX_SSSE3 vpx_dsp/x86/inv_txfm_ssse3.c vpx_dsp/x86/vpx_subpixel_8t_intrin_ssse3.c)
	set(KP_VPX_AVX2 vpx_dsp/x86/inv_txfm_avx2.c vpx_dsp/x86/loopfilter_avx2.c vpx_dsp/x86/vpx_subpixel_8t_intrin_avx2.c)
	set(KP_VPX_ASM
		vpx_dsp/x86/intrapred_sse2.asm vpx_dsp/x86/intrapred_ssse3.asm vpx_dsp/x86/inv_wht_sse2.asm
		vpx_dsp/x86/vpx_convolve_copy_sse2.asm vpx_dsp/x86/vpx_subpixel_8t_sse2.asm vpx_dsp/x86/vpx_subpixel_8t_ssse3.asm
		vpx_dsp/x86/vpx_subpixel_bilinear_sse2.asm vpx_dsp/x86/vpx_subpixel_bilinear_ssse3.asm vpx_ports/emms_mmx.asm)
	list(APPEND KP_VPX_SOURCES ${KP_VPX_SSE2} ${KP_VPX_SSSE3} ${KP_VPX_AVX2} ${KP_VPX_ASM})
	if(MSVC)
		set(KP_VPX_FLAGS AVX2 /arch:AVX2) # SSE2 and SSSE3 need no flag of MSVC's
	else()
		set(KP_VPX_FLAGS SSE2 -msse2 SSSE3 -mssse3 AVX2 -mavx2)
	endif()
	while(KP_VPX_FLAGS)
		list(POP_FRONT KP_VPX_FLAGS group flag)
		list(TRANSFORM KP_VPX_${group} PREPEND ${KP_VPX_DIR}/ OUTPUT_VARIABLE files)
		set_source_files_properties(${files} PROPERTIES COMPILE_OPTIONS ${flag})
	endwhile()
else()
	set(KP_VPX_ARCH generic)
endif()
message(STATUS "libvpx: the ${KP_VPX_ARCH} build")
list(TRANSFORM KP_VPX_SOURCES PREPEND ${KP_VPX_DIR}/)
set(KP_VPX_GENERATED ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vpx/generated)

add_library(kp_vpx STATIC ${KP_VPX_SOURCES} ${KP_VPX_GENERATED}/${KP_VPX_ARCH}/vpx_config.c)
# generated/ first: its vpx_config.h, vpx_version.h and *_rtcd.h stand in for the ones configure writes (and for the
# .asm its vpx_config.asm, beside the source's third_party/x86inc/x86inc.asm)
target_include_directories(kp_vpx PRIVATE ${KP_VPX_GENERATED}/${KP_VPX_ARCH} ${KP_VPX_GENERATED} ${KP_VPX_DIR})
target_include_directories(kp_vpx PUBLIC ${KP_VPX_DIR})
set_target_properties(kp_vpx PROPERTIES C_STANDARD 11 C_EXTENSIONS ON POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
	target_compile_options(kp_vpx PRIVATE $<$<COMPILE_LANGUAGE:C>:-w>)
endif()
