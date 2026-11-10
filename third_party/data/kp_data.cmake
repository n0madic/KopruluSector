# The readers of the original data (ADR 0018): StormLib (the 1.17 install's MPQ archives) and CascLib (StarCraft:
# Remastered's CASC storage), over one zlib and one bzip2, from the pinned sources `tools/toolchain/fetch.sh data`
# fetches. Their own copies of zlib (1.2.5, 1.2.11) and bzip2 (1.0.5) are left out: both libraries take the shared ones
# as a system's (StormLib's __SYS_ZLIB and __SYS_BZLIB, CascLib's CASC_USE_SYSTEM_ZLIB), and both take StormLib's Jenkins
# hash (kp_jenkins: CascLib's lookup3.c is the same file but for whitespace). The source lists are those of the libraries' own
# CMakeLists.txt at these versions (StormLib's SRC_FILES, TOMCRYPT_FILES and TOMMATH_FILES; CascLib's SRC_FILES).
# Built without the project's warnings, like kp_stb and kp_vpx. Both libraries are C++ with C interfaces; CascLib.h
# includes C++ headers, so C code reaches CascLib through a C++ file of its own.
set(KP_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tools/toolchain/data)
set(KP_STORM_DIR ${KP_DATA_DIR}/StormLib-9.40)
set(KP_CASC_DIR ${KP_DATA_DIR}/CascLib-3.0)
set(KP_ZLIB_DIR ${KP_DATA_DIR}/zlib-1.3.2)
set(KP_BZIP2_DIR ${KP_DATA_DIR}/bzip2-1.0.8)
foreach(probe ${KP_STORM_DIR}/src/StormLib.h ${KP_CASC_DIR}/src/CascLib.h ${KP_ZLIB_DIR}/zlib.h ${KP_BZIP2_DIR}/bzlib.h)
	if(NOT EXISTS ${probe})
		message(FATAL_ERROR "the data readers need their sources: run tools/toolchain/fetch.sh data")
	endif()
endforeach()

# Their own dialects (bzip2's fdopen is POSIX, which strict C99 hides), without the project's warnings
function(kp_data_quiet name)
	set_target_properties(${name} PROPERTIES C_EXTENSIONS ON)
	if(NOT MSVC)
		target_compile_options(${name} PRIVATE -w)
	endif()
endfunction()

set(KP_ZLIB_SOURCES adler32.c compress.c crc32.c deflate.c inffast.c inflate.c inftrees.c trees.c uncompr.c zutil.c)
list(TRANSFORM KP_ZLIB_SOURCES PREPEND ${KP_ZLIB_DIR}/)
add_library(kp_zlib STATIC ${KP_ZLIB_SOURCES})
target_include_directories(kp_zlib PUBLIC ${KP_ZLIB_DIR})
kp_data_quiet(kp_zlib)

set(KP_BZIP2_SOURCES blocksort.c bzlib.c compress.c crctable.c decompress.c huffman.c randtable.c)
list(TRANSFORM KP_BZIP2_SOURCES PREPEND ${KP_BZIP2_DIR}/)
add_library(kp_bzip2 STATIC ${KP_BZIP2_SOURCES})
target_include_directories(kp_bzip2 PUBLIC ${KP_BZIP2_DIR})
kp_data_quiet(kp_bzip2)

# Bob Jenkins' lookup3 (public domain), which both libraries call: StormLib's copy, once
add_library(kp_jenkins STATIC ${KP_STORM_DIR}/src/jenkins/lookup3.c)
kp_data_quiet(kp_jenkins)

set(KP_STORM_SOURCES
	src/adpcm/adpcm.cpp src/huffman/huff.cpp src/lzma/C/LzFind.c src/lzma/C/LzmaDec.c
	src/lzma/C/LzmaEnc.c src/pklib/explode.c src/pklib/implode.c src/sparse/sparse.cpp src/FileStream.cpp
	src/SBaseCommon.cpp src/SBaseDumpData.cpp src/SBaseFileTable.cpp src/SBaseSubTypes.cpp src/SCompression.cpp
	src/SFileAddFile.cpp src/SFileAttributes.cpp src/SFileCompactArchive.cpp src/SFileCreateArchive.cpp
	src/SFileExtractFile.cpp src/SFileFindFile.cpp src/SFileGetFileInfo.cpp src/SFileListFile.cpp
	src/SFileOpenArchive.cpp src/SFileOpenFileEx.cpp src/SFilePatchArchives.cpp src/SFileReadFile.cpp
	src/SFileVerify.cpp src/SMemUtf8.cpp src/libtomcrypt/src/pk/rsa/rsa_verify_simple.c
	src/libtomcrypt/src/misc/crypt_libc.c
	src/libtomcrypt/src/hashes/hash_memory.c src/libtomcrypt/src/hashes/md5.c src/libtomcrypt/src/hashes/sha1.c
	src/libtomcrypt/src/hashes/sha256.c src/libtomcrypt/src/math/ltm_desc.c src/libtomcrypt/src/math/multi.c
	src/libtomcrypt/src/math/rand_prime.c src/libtomcrypt/src/misc/base64_decode.c
	src/libtomcrypt/src/misc/crypt_argchk.c src/libtomcrypt/src/misc/crypt_find_hash.c
	src/libtomcrypt/src/misc/crypt_find_prng.c src/libtomcrypt/src/misc/crypt_hash_descriptor.c
	src/libtomcrypt/src/misc/crypt_hash_is_valid.c src/libtomcrypt/src/misc/crypt_ltc_mp_descriptor.c
	src/libtomcrypt/src/misc/crypt_prng_descriptor.c src/libtomcrypt/src/misc/crypt_prng_is_valid.c
	src/libtomcrypt/src/misc/crypt_register_hash.c src/libtomcrypt/src/misc/crypt_register_prng.c
	src/libtomcrypt/src/misc/zeromem.c src/libtomcrypt/src/pk/asn1/der_decode_bit_string.c
	src/libtomcrypt/src/pk/asn1/der_decode_boolean.c src/libtomcrypt/src/pk/asn1/der_decode_choice.c
	src/libtomcrypt/src/pk/asn1/der_decode_ia5_string.c src/libtomcrypt/src/pk/asn1/der_decode_integer.c
	src/libtomcrypt/src/pk/asn1/der_decode_object_identifier.c src/libtomcrypt/src/pk/asn1/der_decode_octet_string.c
	src/libtomcrypt/src/pk/asn1/der_decode_printable_string.c src/libtomcrypt/src/pk/asn1/der_decode_sequence_ex.c
	src/libtomcrypt/src/pk/asn1/der_decode_sequence_flexi.c src/libtomcrypt/src/pk/asn1/der_decode_sequence_multi.c
	src/libtomcrypt/src/pk/asn1/der_decode_short_integer.c src/libtomcrypt/src/pk/asn1/der_decode_utctime.c
	src/libtomcrypt/src/pk/asn1/der_decode_utf8_string.c src/libtomcrypt/src/pk/asn1/der_encode_bit_string.c
	src/libtomcrypt/src/pk/asn1/der_encode_boolean.c src/libtomcrypt/src/pk/asn1/der_encode_ia5_string.c
	src/libtomcrypt/src/pk/asn1/der_encode_integer.c src/libtomcrypt/src/pk/asn1/der_encode_object_identifier.c
	src/libtomcrypt/src/pk/asn1/der_encode_octet_string.c src/libtomcrypt/src/pk/asn1/der_encode_printable_string.c
	src/libtomcrypt/src/pk/asn1/der_encode_sequence_ex.c src/libtomcrypt/src/pk/asn1/der_encode_sequence_multi.c
	src/libtomcrypt/src/pk/asn1/der_encode_set.c src/libtomcrypt/src/pk/asn1/der_encode_setof.c
	src/libtomcrypt/src/pk/asn1/der_encode_short_integer.c src/libtomcrypt/src/pk/asn1/der_encode_utctime.c
	src/libtomcrypt/src/pk/asn1/der_encode_utf8_string.c src/libtomcrypt/src/pk/asn1/der_length_bit_string.c
	src/libtomcrypt/src/pk/asn1/der_length_boolean.c src/libtomcrypt/src/pk/asn1/der_length_ia5_string.c
	src/libtomcrypt/src/pk/asn1/der_length_integer.c src/libtomcrypt/src/pk/asn1/der_length_object_identifier.c
	src/libtomcrypt/src/pk/asn1/der_length_octet_string.c src/libtomcrypt/src/pk/asn1/der_length_printable_string.c
	src/libtomcrypt/src/pk/asn1/der_length_sequence.c src/libtomcrypt/src/pk/asn1/der_length_utctime.c
	src/libtomcrypt/src/pk/asn1/der_sequence_free.c src/libtomcrypt/src/pk/asn1/der_length_utf8_string.c
	src/libtomcrypt/src/pk/asn1/der_length_short_integer.c src/libtomcrypt/src/pk/ecc/ltc_ecc_map.c
	src/libtomcrypt/src/pk/ecc/ltc_ecc_mul2add.c src/libtomcrypt/src/pk/ecc/ltc_ecc_mulmod.c
	src/libtomcrypt/src/pk/ecc/ltc_ecc_points.c src/libtomcrypt/src/pk/ecc/ltc_ecc_projective_add_point.c
	src/libtomcrypt/src/pk/ecc/ltc_ecc_projective_dbl_point.c src/libtomcrypt/src/pk/pkcs1/pkcs_1_mgf1.c
	src/libtomcrypt/src/pk/pkcs1/pkcs_1_oaep_decode.c src/libtomcrypt/src/pk/pkcs1/pkcs_1_pss_decode.c
	src/libtomcrypt/src/pk/pkcs1/pkcs_1_pss_encode.c src/libtomcrypt/src/pk/pkcs1/pkcs_1_v1_5_decode.c
	src/libtomcrypt/src/pk/pkcs1/pkcs_1_v1_5_encode.c src/libtomcrypt/src/pk/rsa/rsa_exptmod.c
	src/libtomcrypt/src/pk/rsa/rsa_free.c src/libtomcrypt/src/pk/rsa/rsa_import.c
	src/libtomcrypt/src/pk/rsa/rsa_make_key.c src/libtomcrypt/src/pk/rsa/rsa_sign_hash.c
	src/libtomcrypt/src/pk/rsa/rsa_verify_hash.c
	src/libtommath/bncore.c src/libtommath/bn_fast_mp_invmod.c src/libtommath/bn_fast_mp_montgomery_reduce.c
	src/libtommath/bn_fast_s_mp_mul_digs.c src/libtommath/bn_fast_s_mp_mul_high_digs.c src/libtommath/bn_fast_s_mp_sqr.c
	src/libtommath/bn_mp_2expt.c src/libtommath/bn_mp_abs.c src/libtommath/bn_mp_add.c src/libtommath/bn_mp_addmod.c
	src/libtommath/bn_mp_add_d.c src/libtommath/bn_mp_and.c src/libtommath/bn_mp_clamp.c src/libtommath/bn_mp_clear.c
	src/libtommath/bn_mp_clear_multi.c src/libtommath/bn_mp_cmp.c src/libtommath/bn_mp_cmp_d.c
	src/libtommath/bn_mp_cmp_mag.c src/libtommath/bn_mp_cnt_lsb.c src/libtommath/bn_mp_copy.c
	src/libtommath/bn_mp_count_bits.c src/libtommath/bn_mp_div.c src/libtommath/bn_mp_div_2.c
	src/libtommath/bn_mp_div_2d.c src/libtommath/bn_mp_div_3.c src/libtommath/bn_mp_div_d.c
	src/libtommath/bn_mp_dr_is_modulus.c src/libtommath/bn_mp_dr_reduce.c src/libtommath/bn_mp_dr_setup.c
	src/libtommath/bn_mp_exch.c src/libtommath/bn_mp_exptmod.c src/libtommath/bn_mp_exptmod_fast.c
	src/libtommath/bn_mp_expt_d.c src/libtommath/bn_mp_exteuclid.c src/libtommath/bn_mp_fread.c
	src/libtommath/bn_mp_fwrite.c src/libtommath/bn_mp_gcd.c src/libtommath/bn_mp_get_int.c src/libtommath/bn_mp_grow.c
	src/libtommath/bn_mp_init.c src/libtommath/bn_mp_init_copy.c src/libtommath/bn_mp_init_multi.c
	src/libtommath/bn_mp_init_set.c src/libtommath/bn_mp_init_set_int.c src/libtommath/bn_mp_init_size.c
	src/libtommath/bn_mp_invmod.c src/libtommath/bn_mp_invmod_slow.c src/libtommath/bn_mp_is_square.c
	src/libtommath/bn_mp_jacobi.c src/libtommath/bn_mp_karatsuba_mul.c src/libtommath/bn_mp_karatsuba_sqr.c
	src/libtommath/bn_mp_lcm.c src/libtommath/bn_mp_lshd.c src/libtommath/bn_mp_mod.c src/libtommath/bn_mp_mod_2d.c
	src/libtommath/bn_mp_mod_d.c src/libtommath/bn_mp_montgomery_calc_normalization.c
	src/libtommath/bn_mp_montgomery_reduce.c src/libtommath/bn_mp_montgomery_setup.c src/libtommath/bn_mp_mul.c
	src/libtommath/bn_mp_mulmod.c src/libtommath/bn_mp_mul_2.c src/libtommath/bn_mp_mul_2d.c
	src/libtommath/bn_mp_mul_d.c src/libtommath/bn_mp_neg.c src/libtommath/bn_mp_n_root.c src/libtommath/bn_mp_or.c
	src/libtommath/bn_mp_prime_fermat.c src/libtommath/bn_mp_prime_is_divisible.c src/libtommath/bn_mp_prime_is_prime.c
	src/libtommath/bn_mp_prime_miller_rabin.c src/libtommath/bn_mp_prime_next_prime.c
	src/libtommath/bn_mp_prime_rabin_miller_trials.c src/libtommath/bn_mp_prime_random_ex.c
	src/libtommath/bn_mp_radix_size.c src/libtommath/bn_mp_radix_smap.c src/libtommath/bn_mp_rand.c
	src/libtommath/bn_mp_read_radix.c src/libtommath/bn_mp_read_signed_bin.c src/libtommath/bn_mp_read_unsigned_bin.c
	src/libtommath/bn_mp_reduce.c src/libtommath/bn_mp_reduce_2k.c src/libtommath/bn_mp_reduce_2k_l.c
	src/libtommath/bn_mp_reduce_2k_setup.c src/libtommath/bn_mp_reduce_2k_setup_l.c src/libtommath/bn_mp_reduce_is_2k.c
	src/libtommath/bn_mp_reduce_is_2k_l.c src/libtommath/bn_mp_reduce_setup.c src/libtommath/bn_mp_rshd.c
	src/libtommath/bn_mp_set.c src/libtommath/bn_mp_set_int.c src/libtommath/bn_mp_shrink.c
	src/libtommath/bn_mp_signed_bin_size.c src/libtommath/bn_mp_sqr.c src/libtommath/bn_mp_sqrmod.c
	src/libtommath/bn_mp_sqrt.c src/libtommath/bn_mp_sub.c src/libtommath/bn_mp_submod.c src/libtommath/bn_mp_sub_d.c
	src/libtommath/bn_mp_toom_mul.c src/libtommath/bn_mp_toom_sqr.c src/libtommath/bn_mp_toradix.c
	src/libtommath/bn_mp_toradix_n.c src/libtommath/bn_mp_to_signed_bin.c src/libtommath/bn_mp_to_signed_bin_n.c
	src/libtommath/bn_mp_to_unsigned_bin.c src/libtommath/bn_mp_to_unsigned_bin_n.c
	src/libtommath/bn_mp_unsigned_bin_size.c src/libtommath/bn_mp_xor.c src/libtommath/bn_mp_zero.c
	src/libtommath/bn_prime_tab.c src/libtommath/bn_reverse.c src/libtommath/bn_s_mp_add.c
	src/libtommath/bn_s_mp_exptmod.c src/libtommath/bn_s_mp_mul_digs.c src/libtommath/bn_s_mp_mul_high_digs.c
	src/libtommath/bn_s_mp_sqr.c src/libtommath/bn_s_mp_sub.c)
list(TRANSFORM KP_STORM_SOURCES PREPEND ${KP_STORM_DIR}/)
add_library(kp_storm STATIC ${KP_STORM_SOURCES})
target_include_directories(kp_storm SYSTEM PUBLIC ${KP_STORM_DIR}/src)
# _7ZIP_ST and BZ_STRICT_ANSI as StormLib's CMakeLists.txt sets them; the shared zlib and bzip2 as the system's
target_compile_definitions(kp_storm PRIVATE _7ZIP_ST BZ_STRICT_ANSI __SYS_ZLIB __SYS_BZLIB)
target_compile_definitions(kp_storm PUBLIC __STORMLIB_NO_STATIC_LINK__)
target_link_libraries(kp_storm PRIVATE kp_zlib kp_bzip2 kp_jenkins)
if(WIN32)
	target_link_libraries(kp_storm PRIVATE wininet)
endif()
kp_data_quiet(kp_storm)

set(KP_CASC_SOURCES
	src/common/Common.cpp src/common/Directory.cpp src/common/Csv.cpp src/common/FileStream.cpp src/common/FileTree.cpp
	src/common/ListFile.cpp src/common/Mime.cpp src/common/RootHandler.cpp src/common/Sockets.cpp src/hashes/md5.cpp
	src/hashes/sha1.cpp src/overwatch/apm.cpp src/overwatch/cmf.cpp src/overwatch/aes.cpp src/CascDecompress.cpp
	src/CascDecrypt.cpp src/CascDumpData.cpp src/CascFiles.cpp src/CascFindFile.cpp src/CascIndexFiles.cpp
	src/CascOpenFile.cpp src/CascOpenStorage.cpp src/CascReadFile.cpp src/CascRootFile_Diablo3.cpp
	src/CascRootFile_Install.cpp src/CascRootFile_MNDX.cpp src/CascRootFile_Text.cpp src/CascRootFile_TVFS.cpp
	src/CascRootFile_OW.cpp src/CascRootFile_WoW.cpp)
list(TRANSFORM KP_CASC_SOURCES PREPEND ${KP_CASC_DIR}/)
add_library(kp_casc STATIC ${KP_CASC_SOURCES})
target_include_directories(kp_casc SYSTEM PUBLIC ${KP_CASC_DIR}/src)
target_compile_definitions(kp_casc PRIVATE CASC_USE_SYSTEM_ZLIB)
target_link_libraries(kp_casc PRIVATE kp_zlib kp_jenkins)
if(WIN32)
	target_link_libraries(kp_casc PRIVATE ws2_32)
endif()
kp_data_quiet(kp_casc)
