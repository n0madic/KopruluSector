# The Arm ASTC encoder as a library of its own (ADR 0013): the sources of the pinned release fetched by
# `tools/toolchain/fetch.sh astc`, for the host's kp_convert and kp_hd_phone only (the product draws ASTC blocks and has no codec).
# Its SIMD comes from the compiler's target (NEON on arm64, SSE2 on x86-64). Included (include()d, not a subdirectory)
# by the top CMakeLists.txt only with -DKP_ASTC=ON. Built without the project's warnings, like kp_stb.
set(KP_ASTC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tools/toolchain/astc/astc-encoder-5.7.0/Source)
if(NOT EXISTS ${KP_ASTC_DIR}/astcenc.h)
	message(FATAL_ERROR "KP_ASTC needs the encoder's source: run tools/toolchain/fetch.sh astc")
endif()
set(KP_ASTC_SOURCES averages_and_directions block_sizes color_quantize color_unquantize compress_symbolic
	compute_variance decompress_symbolic diagnostic_trace entry find_best_partitioning ideal_endpoints_and_weights
	image integer_sequence mathlib mathlib_softfloat partition_tables percentile_tables pick_best_endpoint_format
	quantization symbolic_physical weight_align weight_quant_xfer_tables)
list(TRANSFORM KP_ASTC_SOURCES PREPEND ${KP_ASTC_DIR}/astcenc_)
list(TRANSFORM KP_ASTC_SOURCES APPEND .cpp)
add_library(kp_astcenc STATIC ${KP_ASTC_SOURCES})
target_compile_features(kp_astcenc PRIVATE cxx_std_14)
target_compile_options(kp_astcenc PRIVATE -w)
# Invariance, as the encoder's own CMake sets it by default (ASTCENC_INVARIANCE, Source/cmake_core.cmake): no fused
# multiply-adds and no errno from the math functions, so that compilers and processors write the same blocks (without
# these, macOS's clang, Linux's GCC on arm64 and mingw on x86-64 wrote three different hd2-astc sets). The rest is a
# patch of the sources (tools/toolchain/patches/astc-encoder-5.7.0/): the angle tables from sin and cos in double, since
# each math library's sinf and cosf differ in a few of them.
target_compile_options(kp_astcenc PRIVATE -ffp-contract=off -fno-math-errno
	$<$<CXX_COMPILER_ID:Clang,AppleClang>:-fno-unsafe-math-optimizations>)
