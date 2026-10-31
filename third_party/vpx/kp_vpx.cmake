# libvpx's VP9 decoder as a library of its own (ADR 0015): the sources of the pinned libvpx release fetched by
# `tools/toolchain/fetch.sh vpx`, without an assembler, threads, VP8, the encoder or high bit depth (C11, libvpx 1.17
# uses static_assert), with the few headers configure generates kept in generated/ (regenerate.sh): on arm64 (macOS,
# Android, Linux) its NEON intrinsics (generated/arm64/, NEON is part of every AArch64 CPU; no dotprod, i8mm or SVE,
# which would need a runtime check), elsewhere its C (generated/generic/, the generic-gnu target: x86's SIMD needs
# nasm). Included (include()d, not a subdirectory) by the top CMakeLists.txt only with -DKP_CINEMATICS=ON. Built without
# the project's warnings, like kp_stb.
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

# NEON when the compiler targets AArch64 alone (asked of the compiler, not of CMake's processor variables, which can
# name the host; a macOS build of several architectures takes the C), checked again at each configure
include(CheckCSourceCompiles)
unset(KP_VPX_AARCH64 CACHE)
set(CMAKE_REQUIRED_QUIET ON)
check_c_source_compiles("#if !defined(__aarch64__) && !defined(_M_ARM64)
#error not AArch64
#endif
int main(void) { return 0; }" KP_VPX_AARCH64)
unset(CMAKE_REQUIRED_QUIET)
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
else()
	set(KP_VPX_ARCH generic)
endif()
message(STATUS "libvpx: the ${KP_VPX_ARCH} build")
list(TRANSFORM KP_VPX_SOURCES PREPEND ${KP_VPX_DIR}/)
set(KP_VPX_GENERATED ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vpx/generated)

add_library(kp_vpx STATIC ${KP_VPX_SOURCES} ${KP_VPX_GENERATED}/${KP_VPX_ARCH}/vpx_config.c)
# generated/ first: its vpx_config.h, vpx_version.h and *_rtcd.h stand in for the ones configure writes
target_include_directories(kp_vpx PRIVATE ${KP_VPX_GENERATED}/${KP_VPX_ARCH} ${KP_VPX_GENERATED} ${KP_VPX_DIR})
target_include_directories(kp_vpx PUBLIC ${KP_VPX_DIR})
set_target_properties(kp_vpx PROPERTIES C_STANDARD 11 C_EXTENSIONS ON POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
	target_compile_options(kp_vpx PRIVATE -w)
endif()
