# libvpx's VP9 decoder as a library of its own (ADR 0015): the sources of the pinned libvpx release fetched by
# `tools/toolchain/fetch.sh vpx`, in the C-only build of its generic-gnu target (no assembler, no threads, no VP8, no
# encoder, no high bit depth; C11, libvpx 1.17 uses static_assert), with the few headers configure generates kept in generated/ (regenerate.sh). Included
# (include()d, not a subdirectory) by the top CMakeLists.txt only with -DKP_CINEMATICS=ON. Built without the project's warnings, like kp_stb.
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
list(TRANSFORM KP_VPX_SOURCES PREPEND ${KP_VPX_DIR}/)

add_library(kp_vpx STATIC ${KP_VPX_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vpx/generated/vpx_config.c)
# generated/ first: its vpx_config.h, vpx_version.h and *_rtcd.h stand in for the ones configure writes
target_include_directories(kp_vpx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vpx/generated ${KP_VPX_DIR})
target_include_directories(kp_vpx PUBLIC ${KP_VPX_DIR})
set_target_properties(kp_vpx PROPERTIES C_STANDARD 11 C_EXTENSIONS ON POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
	target_compile_options(kp_vpx PRIVATE -w)
endif()
