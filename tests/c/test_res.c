/* The reading of the original data (ADR 0018): the SHA-256 the input manifest's digests are checked with. */
#include "kp_test.h"

#include "kp_sha256.h"

static const char *hex_of(KpT *T, const void *data, size_t size)
{
	uint8_t d[KP_SHA256_SIZE];
	char hex[2 * KP_SHA256_SIZE + 1];
	kp_sha256(data, size, d);
	kp_sha256_hex(d, hex);
	return kp_t_strdup(T, hex);
}

/* FIPS 180-4's examples (NIST's SHA256.pdf, SHA2_Additional.pdf) */
static void sha256_vectors(KpT *T)
{
	static const char TWO_BLOCKS[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	kp_t_eq_str(T, hex_of(T, "", 0), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "empty");
	kp_t_eq_str(T, hex_of(T, "abc", 3), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "abc");
	kp_t_eq_str(T, hex_of(T, TWO_BLOCKS, sizeof TWO_BLOCKS - 1),
		"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "448 bits");
}

/* A million 'a's in pieces of every size from 1 to 130 bytes: the block buffer filled across its boundaries. */
static void sha256_pieces(KpT *T)
{
	static uint8_t a[130];
	KpSha256 s;
	uint8_t d[KP_SHA256_SIZE];
	char hex[2 * KP_SHA256_SIZE + 1];
	size_t left = 1000000, piece = 1;
	memset(a, 'a', sizeof a);
	kp_sha256_init(&s);
	while (left) {
		size_t n = piece < left ? piece : left;
		kp_sha256_update(&s, a, n);
		left -= n;
		piece = piece % sizeof a + 1;
	}
	kp_sha256_final(&s, d);
	kp_sha256_hex(d, hex);
	kp_t_eq_str(T, hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "a million a");
}

/* Messages of 55, 56 and 64 bytes: the length's padding in the same block, in the next one, after a full one. */
static void sha256_padding_edges(KpT *T)
{
	static const struct {
		size_t size;
		const char *hex;
	} CASES[] = {
		{ 55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318" },
		{ 56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a" },
		{ 64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb" },
	};
	static uint8_t a[64];
	size_t k;
	memset(a, 'a', sizeof a);
	for (k = 0; k < sizeof CASES / sizeof CASES[0]; k++)
		kp_t_eq_str(T, hex_of(T, a, CASES[k].size), CASES[k].hex, "%u bytes of a", (unsigned)CASES[k].size);
}

static const KpTest TESTS[] = {
	{ "sha256_vectors", sha256_vectors, 0 },
	{ "sha256_pieces", sha256_pieces, 0 },
	{ "sha256_padding_edges", sha256_padding_edges, 0 },
};
KP_SUITE(kp_suite_res, "test_res", TESTS);
