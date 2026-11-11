/* The reading of the original data (ADR 0018): the SHA-256 the input manifest's digests are checked with, kp_res's
 * sources over archives the tests write with StormLib (a 1.17 install of a test profile, repacks), its Remastered
 * rules, and with KP_TEST_ORIGINAL and KP_TEST_SCR the real installs. */
#include "kp_test_app.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kp_mpq.h"
#include "kp_res.h"
#include "kp_sha256.h"
#include "kp_ui_data.h"

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

/* kp_hex_decode: a digest back from kp_sha256_hex, either case, a digit that is not one refused */
static void sha256_hex_decodes(KpT *T)
{
	uint8_t d[KP_SHA256_SIZE], back[KP_SHA256_SIZE], two[2];
	char hex[2 * KP_SHA256_SIZE + 1];
	kp_sha256("abc", 3, d);
	kp_sha256_hex(d, hex);
	kp_t_eq(T, kp_hex_decode(hex, back, KP_SHA256_SIZE), 0, "decoded");
	kp_t_ok(T, memcmp(d, back, sizeof d) == 0, "the digest back");
	kp_t_ok(T, kp_hex_decode("aB0f", two, 2) == 0 && two[0] == 0xab && two[1] == 0x0f, "either case");
	kp_t_eq(T, kp_hex_decode("a g0", two, 2), -1, "a space");
	kp_t_eq(T, kp_hex_decode("+1", two, 1), -1, "a sign");
	kp_t_eq(T, kp_hex_decode("ab", two, 2), -1, "short");
}

/* --- kp_res over archives of the test's own --- */

#define DIR_FILES 16

/* A folder of the test's own under build/ (kp_t_temp_dir, removed at the end) and the paths of the files written to it. */
typedef struct {
	const char *dir;
	char files[DIR_FILES][320];
	int count;
} Dir;

/* kp_res_close as a deferred cleanup (a call through another function type is undefined) */
static void res_close(void *p) { kp_res_close((KpRes *)p); }

static Dir *dir_new(KpT *T)
{
	Dir *d = (Dir *)calloc(1, sizeof *d);
	kp_t_ok(T, d != NULL, "out of memory");
	kp_t_defer(T, free, d);
	d->dir = kp_t_temp_dir(T, NULL);
	return d;
}

static const char *dir_path(KpT *T, Dir *d, const char *name)
{
	kp_t_ok(T, d->count < DIR_FILES, "too many files");
	snprintf(d->files[d->count], sizeof d->files[0], "%s/%s", d->dir, name);
	return d->files[d->count++];
}

static void dir_write(KpT *T, Dir *d, const char *name, const char *text)
{
	const char *path = dir_path(T, d, name);
	FILE *f = fopen(path, "wb");
	kp_t_ok(T, f != NULL && fwrite(text, 1, strlen(text), f) == strlen(text) && fclose(f) == 0, "cannot write %s", path);
}

/* An archive of name -> text pairs (names NULL-terminated) written into d. */
static void dir_mpq(KpT *T, Dir *d, const char *archive, const char *const *pairs)
{
	char err[512];
	KpMpq *m = kp_mpq_create(dir_path(T, d, archive), 64, err, sizeof err);
	int k;
	kp_t_ok(T, m != NULL, "%s", err);
	for (k = 0; pairs[k]; k += 2)
		kp_t_ok(T, kp_mpq_add(m, pairs[k], pairs[k + 1], strlen(pairs[k + 1]), k % 4 == 0 ? KP_MPQ_ZLIB : KP_MPQ_STORE, err, sizeof err) == 0, "%s", err);
	kp_t_eq(T, kp_mpq_close(m), 0, "%s written", archive);
}

/* The test profile: Game.exe, the core archives A.mpq and B.mpq, the campaign archive C.mpq; its manifest. */
static const char ONE[] = "the first copy of one", ONE_SHADOWED[] = "B's copy of one", TWO[] = "two as listed",
	TWO_CHANGED[] = "two, changed", THREE[] = "three";
static const char *const ABSENT[] = { "a/gone.dat" };
static const KpDefsArchive ARCHIVES[] = { { "A.mpq", 0, "core" }, { "B.mpq", 0, "core" }, { "C.mpq", 0, "campaign" } };

typedef struct {
	KpDefsProfile profile;
	KpDefsInput files[5]; /* profile_init's three; a test may add more */
	char log[4096];
} Profile;

static void profile_init(KpT *T, Profile *p)
{
	memset(p, 0, sizeof *p);
	/* sorted by name */
	p->files[0].name = "a/one.dat";
	p->files[0].size = (uint32_t)strlen(ONE);
	p->files[0].sha256 = hex_of(T, ONE, strlen(ONE));
	p->files[1].name = "a/three.dat";
	p->files[1].size = (uint32_t)strlen(THREE);
	p->files[1].sha256 = hex_of(T, THREE, strlen(THREE));
	p->files[2].name = "a/two.dat";
	p->files[2].size = (uint32_t)strlen(TWO);
	p->files[2].sha256 = hex_of(T, TWO, strlen(TWO));
	p->profile.id = "test-1";
	p->profile.executable = "Game.exe";
	p->profile.archives = ARCHIVES;
	p->profile.archive_count = 3;
	p->profile.files = p->files;
	p->profile.file_count = 3;
	p->profile.absent = ABSENT;
	p->profile.absent_count = 1;
}

static void profile_log(void *ctx, const char *line)
{
	Profile *p = (Profile *)ctx;
	size_t n = strlen(p->log);
	snprintf(p->log + n, sizeof p->log - n, "%s\n", line);
}

/* An install of the test profile: A.mpq has one, B.mpq a shadowed one, a changed two and an unlisted file; no C.mpq. */
static Dir *test_install(KpT *T)
{
	static const char *const A[] = { "a\\one.dat", ONE, NULL };
	static const char *const B[] = { "a\\one.dat", ONE_SHADOWED, "a\\two.dat", TWO_CHANGED, "a\\extra.txt", "unlisted", NULL };
	Dir *d = dir_new(T);
	dir_write(T, d, "Game.exe", "MZ");
	dir_mpq(T, d, "A.mpq", A);
	dir_mpq(T, d, "B.mpq", B);
	return d;
}

static KpRes *open_res(KpT *T, Profile *p, const char *const *dirs, int count)
{
	KpResConfig c;
	char err[512];
	KpRes *r;
	memset(&c, 0, sizeof c);
	c.profile = &p->profile;
	c.log = profile_log;
	c.log_ctx = p;
	r = kp_res_open(dirs, count, &c, err, sizeof err);
	kp_t_ok(T, r != NULL, "%s", err);
	kp_t_defer(T, res_close, r);
	return r;
}

/* reads name: its status, its text (NULL when nothing was read) and the error in err */
static const char *read_text(KpT *T, KpRes *r, const char *name, KpResStatus *st, char *err, size_t err_size)
{
	size_t size = 0;
	uint8_t *b = kp_res_read(r, name, &size, st, err, err_size);
	char *s;
	if (!b) return NULL;
	kp_t_eq(T, b[size], 0, "%s NUL terminated", name);
	s = kp_t_strdup(T, (const char *)b);
	free(b);
	return s;
}

static void res_reads_a_1_17_install_checked(KpT *T)
{
	Profile p;
	Dir *d;
	KpRes *r;
	KpResStatus st;
	char err[512];
	const char *s, *dirs[1];
	profile_init(T, &p);
	d = test_install(T);
	kp_t_eq(T, kp_res_detect(d->dir, &p.profile), KP_RES_SOURCE_MPQ, "detected");
	dirs[0] = d->dir;
	r = open_res(T, &p, dirs, 1);
	kp_t_eq(T, kp_res_install(r), KP_RES_SOURCE_MPQ, "the install");
	kp_t_ok(T, strstr(p.log, "has no C.mpq") != NULL, "the missing campaign archive logged: %s", p.log);
	s = read_text(T, r, "A\\ONE.dat", &st, err, sizeof err);
	kp_t_eq_str(T, s, ONE, "the first archive's copy");
	kp_t_eq(T, st, KP_RES_EXACT, "one's status");
	s = read_text(T, r, "a/two.dat", &st, err, sizeof err);
	kp_t_ok(T, s == NULL, "a copy that differs from the manifest is refused");
	kp_t_eq(T, st, KP_RES_REFUSED, "two's status");
	kp_t_ok(T, strstr(err, "B.mpq: a\\two.dat has size 12") != NULL && strstr(err, "profile test-1 expects size 13") != NULL, "%s", err);
	s = read_text(T, r, "a\\extra.txt", &st, err, sizeof err);
	kp_t_eq_str(T, s, "unlisted", "an unlisted file read");
	kp_t_eq(T, st, KP_RES_UNLISTED, "extra's status");
	kp_t_ok(T, strstr(p.log, "a\\extra.txt is not in the input manifest") != NULL, "logged: %s", p.log);
	kp_t_ok(T, read_text(T, r, "a\\gone.dat", &st, err, sizeof err) == NULL && st == KP_RES_ABSENT && !err[0], "absent: %s", err);
	kp_t_ok(T, read_text(T, r, "a\\three.dat", &st, err, sizeof err) == NULL && st == KP_RES_MISSING && !err[0], "missing: %s", err);
	kp_t_ok(T, kp_res_exists(r, "a/one.dat") && kp_res_exists(r, "a/extra.txt"), "exists");
	kp_t_ok(T, !kp_res_exists(r, "a/gone.dat") && !kp_res_exists(r, "a/three.dat"), "does not exist");
}

/* A damaged copy (a disc copied with a bad read, issue #2): its archive lists it at the manifest's size, its first
 * sectors read but not all of them. It exists and its head reads (a mission's WAV probe, kp_files_wav: the sound keeps
 * its index); a whole read fails with the reason (the line plays nothing, kp_app_audio.c). */
static void res_damaged_copy_reads_its_head(KpT *T)
{
	enum { BIG = 40000, HEAD = 4096, CUT = 32 };
	static char big[BIG + 1];
	static const char *const B[] = { "a\\two.dat", TWO, NULL };
	const char *pairs[5], *dirs[1];
	char path[400], err[512];
	uint8_t *mpq, *head;
	size_t mpq_size = 0, got = 0;
	uint64_t total = 0;
	uint32_t x = 1, data_end, at, k;
	Profile p;
	Dir *d = dir_new(T);
	KpRes *r;
	KpResStatus st;
	FILE *f;
	for (k = 0; k < BIG; k++) big[k] = (char)('a' + ((x = x * 1103515245u + 12345u) >> 16) % 16); /* ~10 zlib sectors */
	big[BIG] = 0;
	profile_init(T, &p);
	memmove(p.files + 1, p.files, 3 * sizeof p.files[0]); /* sorted by name: a/bad.wav first */
	p.files[0].name = "a/bad.wav";
	p.files[0].size = BIG;
	p.files[0].sha256 = hex_of(T, big, BIG);
	p.profile.file_count = 4;
	pairs[0] = "a\\bad.wav"; /* zlib-compressed by sector (dir_mpq) */
	pairs[1] = big;
	pairs[2] = "a\\one.dat";
	pairs[3] = ONE;
	pairs[4] = NULL;
	dir_write(T, d, "Game.exe", "MZ");
	dir_mpq(T, d, "A.mpq", pairs);
	dir_mpq(T, d, "B.mpq", B);
	/* the bytes three quarters into the files' data (after the 32-byte header, before the hash table): a late sector of
	 * a\bad.wav's, whose first ones stay whole */
	snprintf(path, sizeof path, "%s/A.mpq", d->dir);
	mpq = kp_t_read(T, path, &mpq_size);
	kp_t_ok(T, mpq_size > 32 && memcmp(mpq, "MPQ\x1a", 4) == 0, "an MPQ with its header first");
	data_end = (uint32_t)mpq[16] | (uint32_t)mpq[17] << 8 | (uint32_t)mpq[18] << 16 | (uint32_t)mpq[19] << 24;
	kp_t_ok(T, data_end > 32 + HEAD && data_end <= mpq_size, "the hash table after the data");
	at = 32 + (data_end - 32) / 4 * 3;
	for (k = 0; k < CUT; k++) mpq[at + k] ^= 0xa5u;
	f = fopen(path, "wb");
	kp_t_ok(T, f != NULL && fwrite(mpq, 1, mpq_size, f) == mpq_size && fclose(f) == 0, "cannot rewrite %s", path);
	dirs[0] = d->dir;
	r = open_res(T, &p, dirs, 1);
	kp_t_ok(T, kp_res_exists(r, "a/bad.wav"), "a damaged copy exists");
	kp_t_ok(T, read_text(T, r, "a\\bad.wav", &st, err, sizeof err) == NULL && st == KP_RES_ERROR && err[0],
		"a whole read fails with the reason: %s", err);
	head = kp_res_head(r, "a\\bad.wav", HEAD, &got, &total, err, sizeof err);
	kp_t_ok(T, head != NULL, "its head reads: %s", err);
	kp_t_ok(T, head && got == HEAD && total == BIG && memcmp(head, big, HEAD) == 0, "its first %d bytes", HEAD);
	free(head);
}

static void res_install_needs_its_core_archives(KpT *T)
{
	static const char *const A[] = { "a\\one.dat", ONE, NULL };
	Profile p;
	Dir *d = dir_new(T);
	const char *dirs[1];
	KpResConfig c;
	char err[512];
	profile_init(T, &p);
	kp_t_eq(T, kp_res_detect(d->dir, &p.profile), KP_RES_SOURCE_NONE, "an empty folder");
	dir_write(T, d, "Game.exe", "MZ");
	dir_mpq(T, d, "A.mpq", A);
	memset(&c, 0, sizeof c);
	c.profile = &p.profile;
	dirs[0] = d->dir;
	kp_t_ok(T, kp_res_open(dirs, 1, &c, err, sizeof err) == NULL, "an install without B.mpq");
	kp_t_ok(T, strstr(err, "no B.mpq (profile test-1)") != NULL, "%s", err);
	dirs[0] = "build/no-such-folder";
	kp_t_ok(T, kp_res_open(dirs, 1, &c, err, sizeof err) == NULL && strstr(err, "holds no data") != NULL, "%s", err);
}

/* The repack's listing of files and their texts (name, text pairs) for profile */
static const char *listing(KpT *T, const char *profile, const char *extra, const char *const *pairs)
{
	char text[2048];
	size_t n;
	int k;
	n = (size_t)snprintf(text, sizeof text, "%s\nprofile %s\n%s", KP_RES_LISTING_MAGIC, profile, extra);
	for (k = 0; pairs[k]; k += 2)
		n += (size_t)snprintf(text + n, sizeof text - n, "%s %u exact %s\n", hex_of(T, pairs[k + 1], strlen(pairs[k + 1])),
			(unsigned)strlen(pairs[k + 1]), pairs[k]);
	return kp_t_strdup(T, text);
}

/* A 1.17 file the simulation is built from is the manifest's to the byte, a repack's its listing's; one that only
 * shows or plays is taken by its size from either (ADR 0018, item 2) */
static void res_hashes_only_what_the_simulation_reads(KpT *T)
{
	static const char PIC[] = "a picture", PIC_EDITED[] = "a pictur3", ONE_EDITED[] = "the first copy of 0ne",
		TWO_SAME_SIZE[] = "two AS listed", WIRE[] = "a wireframe", WIRE_EDITED[] = "a wirefr4me";
	static const char *const A[] = { "a\\one.dat", ONE_EDITED, "a\\pic.pcx", PIC_EDITED, NULL };
	static const char *const B[] = { "a\\three.dat", THREE, "a\\two.dat", TWO, NULL };
	static const char *const LISTED[] = { "a\\two.dat", TWO, "a\\wire.pcx", WIRE, NULL };
	const char *repack[7], *dirs[2], *s;
	Profile p;
	Dir *install, *pack;
	KpRes *r;
	KpResStatus st;
	char err[512];
	profile_init(T, &p);
	/* sorted: a/one.dat, a/pic.pcx, a/three.dat, a/two.dat, a/wire.pcx */
	p.files[3] = p.files[2];
	p.files[2] = p.files[1];
	p.files[1].name = "a/pic.pcx";
	p.files[1].size = (uint32_t)strlen(PIC);
	p.files[1].sha256 = hex_of(T, PIC, strlen(PIC));
	p.files[4].name = "a/wire.pcx";
	p.files[4].size = (uint32_t)strlen(WIRE);
	p.files[4].sha256 = hex_of(T, WIRE, strlen(WIRE));
	p.profile.file_count = 5;
	install = dir_new(T);
	dir_write(T, install, "Game.exe", "MZ");
	dir_mpq(T, install, "A.mpq", A);
	dir_mpq(T, install, "B.mpq", B);
	pack = dir_new(T);
	repack[0] = KP_RES_LISTING;
	repack[1] = listing(T, "test-1", "", LISTED);
	repack[2] = "a\\two.dat";
	repack[3] = TWO_SAME_SIZE;
	repack[4] = "a\\wire.pcx";
	repack[5] = WIRE_EDITED;
	repack[6] = NULL;
	dir_mpq(T, pack, "koprulu.mpq", repack);
	dirs[0] = install->dir;
	dirs[1] = pack->dir;
	r = open_res(T, &p, dirs, 2);
	s = read_text(T, r, "a/pic.pcx", &st, err, sizeof err);
	kp_t_eq_str(T, s, PIC_EDITED, "an install's picture of the manifest's size");
	kp_t_eq(T, st, KP_RES_EXACT, "the picture's status");
	s = read_text(T, r, "a/one.dat", &st, err, sizeof err);
	kp_t_ok(T, s == NULL && st == KP_RES_REFUSED, "an install's DAT of the manifest's size but other bytes is refused");
	kp_t_ok(T, strstr(err, "sha256") != NULL, "%s", err);
	s = read_text(T, r, "a/two.dat", &st, err, sizeof err);
	kp_t_eq_str(T, s, TWO, "a repack's DAT of its listed size but other bytes leaves the install's");
	kp_t_eq(T, st, KP_RES_EXACT, "two's status");
	kp_t_ok(T, strstr(p.log, "a\\two.dat in ") != NULL && strstr(p.log, "differs from its listing") != NULL, "%s", p.log);
	s = read_text(T, r, "a/wire.pcx", &st, err, sizeof err);
	kp_t_eq_str(T, s, WIRE_EDITED, "a repack's picture of its listed size");
	kp_t_eq(T, st, KP_RES_REPACKED, "the wireframe's status");
}

static void res_repack_comes_before_the_install(KpT *T)
{
	/* two as listed (the install's copy is refused), three listed but stored changed (the install has none) */
	static const char *const LISTED[] = { "a\\two.dat", TWO, "a\\three.dat", THREE, NULL };
	const char *repack[7];
	Profile p;
	Dir *install, *pack;
	KpRes *r;
	KpResStatus st;
	char err[512];
	const char *dirs[2], *s;
	profile_init(T, &p);
	install = test_install(T);
	pack = dir_new(T);
	repack[0] = KP_RES_LISTING;
	repack[1] = listing(T, "test-1", "", LISTED);
	repack[2] = "a\\two.dat";
	repack[3] = TWO;
	repack[4] = "a\\three.dat";
	repack[5] = "three, broken";
	repack[6] = NULL;
	dir_mpq(T, pack, "koprulu.mpq", repack);
	kp_t_eq(T, kp_res_detect(pack->dir, &p.profile), KP_RES_SOURCE_REPACK, "detected");
	/* the install named first: the repack still comes first */
	dirs[0] = install->dir;
	dirs[1] = pack->dir;
	r = open_res(T, &p, dirs, 2);
	kp_t_eq(T, kp_res_repacks(r), 1, "one repack");
	s = read_text(T, r, "A\\Two.DAT", &st, err, sizeof err);
	kp_t_eq_str(T, s, TWO, "the repack's copy");
	kp_t_eq(T, st, KP_RES_REPACKED, "two's status");
	s = read_text(T, r, "a/one.dat", &st, err, sizeof err);
	kp_t_eq_str(T, s, ONE, "a file the repack lacks: the install's");
	kp_t_eq(T, st, KP_RES_EXACT, "one's status");
	s = read_text(T, r, "a/three.dat", &st, err, sizeof err);
	kp_t_ok(T, s == NULL && st == KP_RES_MISSING, "a broken copy falls back to the install, which has none");
	kp_t_ok(T, strstr(p.log, "a\\three.dat in ") != NULL && strstr(p.log, "differs from its listing") != NULL, "logged: %s", p.log);
}

static void res_repack_of_another_profile_is_ignored(KpT *T)
{
	static const char *const LISTED[] = { "a\\two.dat", TWO, NULL };
	const char *repack[5];
	Profile p;
	Dir *install, *pack;
	KpRes *r;
	KpResStatus st;
	char err[512];
	const char *dirs[2];
	profile_init(T, &p);
	install = test_install(T);
	pack = dir_new(T);
	repack[0] = KP_RES_LISTING;
	repack[1] = listing(T, "other-2", "", LISTED);
	repack[2] = "a\\two.dat";
	repack[3] = TWO;
	repack[4] = NULL;
	dir_mpq(T, pack, "koprulu.mpq", repack);
	dirs[0] = pack->dir;
	dirs[1] = install->dir;
	r = open_res(T, &p, dirs, 2);
	kp_t_eq(T, kp_res_repacks(r), 0, "no repack");
	kp_t_ok(T, strstr(p.log, "a repack of profile other-2, not test-1") != NULL, "logged: %s", p.log);
	kp_t_ok(T, read_text(T, r, "a/two.dat", &st, err, sizeof err) == NULL && st == KP_RES_REFUSED, "the install's copy");
}

/* A listing refused (here: no profile line) leaves nothing of it behind: its files and groups unapplied, the install
 * read in their place */
static void res_repack_without_its_profile_leaves_nothing(KpT *T)
{
	static const char *const GROUP[] = { "a\\two.dat", TWO, NULL };
	const char *core[5], *group[5];
	char text[1024];
	Profile p;
	Dir *install, *pack;
	KpRes *r;
	KpResStatus st;
	char err[512];
	const char *dirs[2], *s;
	profile_init(T, &p);
	install = test_install(T);
	pack = dir_new(T);
	snprintf(text, sizeof text, "%s\nsource test\ngroup koprulu-g1.mpq a/two\n%s %u exact a\\one.dat\n", KP_RES_LISTING_MAGIC,
		hex_of(T, "one, the repack's", strlen("one, the repack's")), (unsigned)strlen("one, the repack's"));
	core[0] = KP_RES_LISTING;
	core[1] = text;
	core[2] = "a\\one.dat";
	core[3] = "one, the repack's";
	core[4] = NULL;
	group[0] = KP_RES_LISTING;
	group[1] = listing(T, "test-1", "", GROUP);
	group[2] = "a\\two.dat";
	group[3] = TWO;
	group[4] = NULL;
	dir_mpq(T, pack, "koprulu-core.mpq", core);
	dir_mpq(T, pack, "koprulu-g1.mpq", group);
	dirs[0] = pack->dir;
	dirs[1] = install->dir;
	r = open_res(T, &p, dirs, 2);
	kp_t_eq(T, kp_res_repacks(r), 0, "neither the core nor the group it names");
	kp_t_ok(T, strstr(p.log, "is not a repack's listing of profile test-1") != NULL, "logged: %s", p.log);
	s = read_text(T, r, "a/one.dat", &st, err, sizeof err);
	kp_t_eq_str(T, s, ONE, "the install's copy");
	kp_t_eq(T, st, KP_RES_EXACT, "one's status");
	kp_t_ok(T, read_text(T, r, "a/two.dat", &st, err, sizeof err) == NULL && st == KP_RES_REFUSED, "the install's copy of two");
}

static void res_repack_groups_join_the_core(KpT *T)
{
	static const char *const CORE[] = { "a\\one.dat", ONE, NULL };
	static const char *const GROUP[] = { "a\\two.dat", TWO, NULL };
	const char *core[5], *group[5];
	Profile p;
	Dir *pack;
	KpRes *r;
	KpResStatus st;
	char err[512];
	const char *dirs[1], *s;
	int counts[KP_RES_STATUS_COUNT];
	profile_init(T, &p);
	pack = dir_new(T);
	core[0] = KP_RES_LISTING;
	core[1] = listing(T, "test-1", "group koprulu-g1.mpq a/two\ngroup koprulu-g2.mpq b/\n", CORE);
	core[2] = "a\\one.dat";
	core[3] = ONE;
	core[4] = NULL;
	group[0] = KP_RES_LISTING;
	group[1] = listing(T, "test-1", "", GROUP);
	group[2] = "a\\two.dat";
	group[3] = TWO;
	group[4] = NULL;
	dir_mpq(T, pack, "koprulu-core.mpq", core);
	dir_mpq(T, pack, "koprulu-g1.mpq", group);
	dirs[0] = pack->dir;
	r = open_res(T, &p, dirs, 1);
	kp_t_eq(T, kp_res_install(r), KP_RES_SOURCE_NONE, "no install");
	kp_t_eq(T, kp_res_repacks(r), 2, "the core and the group there (g2 is not)");
	s = read_text(T, r, "a/one.dat", &st, err, sizeof err);
	kp_t_eq_str(T, s, ONE, "the core's file");
	s = read_text(T, r, "a/two.dat", &st, err, sizeof err);
	kp_t_eq_str(T, s, TWO, "the group's file");
	kp_t_eq(T, st, KP_RES_REPACKED, "two's status");
	kp_t_ok(T, read_text(T, r, "a/three.dat", &st, err, sizeof err) == NULL && st == KP_RES_MISSING, "no install to fall back to");
	kp_res_counts(r, counts);
	kp_t_eq(T, counts[KP_RES_REPACKED], 2, "repacked");
	kp_t_eq(T, counts[KP_RES_MISSING], 1, "missing");
}

static void le16(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void le32(uint8_t *p, uint32_t v)
{
	le16(p, v);
	le16(p + 2, v >> 16);
}

/* A 16-bit mono WAV of n samples at 22,050 Hz (zeros) into out (44 + 2n bytes): the header the listing keeps. */
static size_t wav_bytes(uint8_t *out, uint32_t samples)
{
	uint32_t data = 2 * samples;
	memcpy(out, "RIFF", 4);
	le32(out + 4, 36 + data);
	memcpy(out + 8, "WAVEfmt ", 8);
	le32(out + 16, 16);
	le16(out + 20, 1);
	le16(out + 22, 1);
	le32(out + 24, 22050);
	le32(out + 28, 44100);
	le16(out + 32, 2);
	le16(out + 34, 16);
	memcpy(out + 36, "data", 4);
	le32(out + 40, data);
	memset(out + 44, 0, data);
	return 44 + (size_t)data;
}

/* The listing's statuses: an exact file whose digest is not the manifest's is refused for the install's, Remastered's
 * own copy ("differs") is read, a WAV stored as Ogg Vorbis answers the WAV's header and size and reads as the Ogg. */
static void res_repack_listing_statuses(KpT *T)
{
	static const char ONE_CHANGED[] = "one, changed", TWO_SCR[] = "two, Remastered's", OGG[] = "OggS, the sound";
	static uint8_t wav[44 + 2 * 1000];
	char text[2048], hex_head[2 * 44 + 1], err[512];
	const char *repack[9], *dirs[2], *s;
	size_t wav_size = wav_bytes(wav, 1000), got = 0, k;
	uint64_t total = 0;
	Profile p;
	Dir *install, *pack;
	KpRes *r;
	KpResStatus st;
	KpFiles files;
	KpWavInfo info;
	KpError e;
	uint8_t *head;
	profile_init(T, &p);
	p.files[3].name = "a/x.wav"; /* sorted after a/two.dat */
	p.files[3].size = (uint32_t)wav_size;
	p.files[3].sha256 = hex_of(T, wav, wav_size);
	p.profile.file_count = 4;
	for (k = 0; k < 44; k++) snprintf(hex_head + 2 * k, 3, "%02x", wav[k]);
	snprintf(text, sizeof text, "%s\nprofile test-1\nsource remastered\n%s %u exact a\\one.dat\n%s %u differs a\\two.dat\n"
		"%s %u exact ogg %s %u %s a\\x.wav\nnot a file's line\n", KP_RES_LISTING_MAGIC,
		hex_of(T, ONE_CHANGED, strlen(ONE_CHANGED)), (unsigned)strlen(ONE_CHANGED), hex_of(T, TWO_SCR, strlen(TWO_SCR)),
		(unsigned)strlen(TWO_SCR), hex_of(T, OGG, strlen(OGG)), (unsigned)strlen(OGG), p.files[3].sha256,
		(unsigned)wav_size, hex_head);
	install = test_install(T);
	pack = dir_new(T);
	repack[0] = KP_RES_LISTING;
	repack[1] = kp_t_strdup(T, text);
	repack[2] = "a\\one.dat";
	repack[3] = ONE_CHANGED;
	repack[4] = "a\\two.dat";
	repack[5] = TWO_SCR;
	repack[6] = "a\\x.wav";
	repack[7] = OGG;
	repack[8] = NULL;
	dir_mpq(T, pack, "koprulu.mpq", repack);
	dirs[0] = install->dir;
	dirs[1] = pack->dir;
	r = open_res(T, &p, dirs, 2);
	kp_t_ok(T, strstr(p.log, "names a\\one.dat as the 1.17 file, which it is not") != NULL, "logged: %s", p.log);
	kp_t_ok(T, strstr(p.log, "1 lines of its listing are not a file's") != NULL, "logged: %s", p.log);
	s = read_text(T, r, "a/one.dat", &st, err, sizeof err);
	kp_t_eq_str(T, s, ONE, "the install's one");
	kp_t_eq(T, st, KP_RES_EXACT, "one's status");
	s = read_text(T, r, "a/two.dat", &st, err, sizeof err);
	kp_t_eq_str(T, s, TWO_SCR, "Remastered's two, as the repack lists it");
	kp_t_eq(T, st, KP_RES_REPACKED, "two's status");
	head = kp_res_head(r, "A\\X.WAV", 4096, &got, &total, err, sizeof err);
	kp_t_ok(T, head != NULL, "%s", err);
	kp_t_ok(T, got == 44 && memcmp(head, wav, 44) == 0, "the WAV's header: %u bytes", (unsigned)got);
	kp_t_eq(T, (int64_t)total, (int64_t)wav_size, "the WAV's size");
	free(head);
	kp_res_files(r, &files);
	kp_t_eq(T, kp_files_wav(&files, "a\\x.wav", &info, &e), 1, "%s", e.msg);
	kp_t_ok(T, info.sample_rate == 22050 && info.data_length == 2000 && info.data_offset == 44, "the WAV's format");
	s = read_text(T, r, "a\\x.wav", &st, err, sizeof err);
	kp_t_eq_str(T, s, OGG, "the sound read as the Ogg");
	kp_t_eq(T, st, KP_RES_REPACKED, NULL);
}

/* A group the core's listing names by prefix: wanted while it is not there, read once it is added (the web). */
static void res_repack_group_wanted_then_added(KpT *T)
{
	static const char *const CORE[] = { "a\\one.dat", ONE, NULL };
	static const char *const GROUP[] = { "campaign\\one\\staredit\\scenario.chk", "a scenario", NULL };
	const char *core[5], *group[5], *dirs[1], *s;
	char file[128], path[512], err[512];
	Profile p;
	Dir *pack;
	KpRes *r;
	KpResStatus st;
	profile_init(T, &p);
	pack = dir_new(T);
	core[0] = KP_RES_LISTING;
	core[1] = listing(T, "test-1", "group koprulu-campaign-one.mpq campaign\\One\\\n", CORE);
	core[2] = "a\\one.dat";
	core[3] = ONE;
	core[4] = NULL;
	dir_mpq(T, pack, "koprulu-core.mpq", core);
	dirs[0] = pack->dir;
	r = open_res(T, &p, dirs, 1);
	kp_t_eq_str(T, kp_res_repack_dir(r), pack->dir, "the core's folder");
	kp_t_eq(T, kp_res_group_wanted(r, "Campaign\\one\\staredit\\scenario.chk", file, sizeof file), 1, "the mission's group");
	kp_t_eq_str(T, file, "koprulu-campaign-one.mpq", NULL);
	kp_t_eq(T, kp_res_group_wanted(r, "a\\one.dat", file, sizeof file), 0, "the core's file is in no group");
	kp_t_ok(T, read_text(T, r, "campaign/one/staredit/scenario.chk", &st, err, sizeof err) == NULL && st == KP_RES_MISSING,
		"missing before the group is there");
	group[0] = KP_RES_LISTING;
	group[1] = listing(T, "test-1", "", GROUP);
	group[2] = GROUP[0];
	group[3] = GROUP[1];
	group[4] = NULL;
	dir_mpq(T, pack, "koprulu-campaign-one.mpq", group);
	snprintf(path, sizeof path, "%s/koprulu-campaign-one.mpq", pack->dir);
	kp_t_eq(T, kp_res_add_repack(r, path, err, sizeof err), 0, "%s", err);
	kp_t_eq(T, kp_res_group_wanted(r, "campaign\\one\\staredit\\scenario.chk", file, sizeof file), 0, "open now");
	s = read_text(T, r, "campaign\\one\\staredit\\scenario.chk", &st, err, sizeof err);
	kp_t_eq_str(T, s, "a scenario", "the group's file");
}

/* The music's lengths (MUSL) read before its group is there are none; read again once it is added, the track's WAV
 * header from the group's listing (the web, whose music comes after the data is built: kp_app_fetch.c music_came). */
static void res_music_lengths_come_with_their_group(KpT *T)
{
	/* MUSI: two tracks, none and music\t.wav (u8 length, the path, kind, fade-in, next) */
	static const uint8_t MUSI[] = { 2, 0, 0, 0, 0, 11, 'm', 'u', 's', 'i', 'c', '\\', 't', '.', 'w', 'a', 'v', 1, 0, 0 };
	static const char *const CORE[] = { "a\\one.dat", ONE, NULL };
	static const char OGG[] = "OggS, the track";
	static uint8_t wav[44 + 2 * 1000];
	static KpDefsTable table;
	char text[1024], hex_head[2 * 44 + 1], path[512], err[512];
	const char *core[5], *group[5], *dirs[1];
	size_t wav_size = wav_bytes(wav, 1000), k;
	Profile p;
	Dir *pack;
	KpRes *r;
	KpFiles files;
	KpUiData d;
	KpError e;
	profile_init(T, &p);
	memcpy(table.fourcc, "MUSI", 5);
	table.data = MUSI;
	table.size = sizeof MUSI;
	p.profile.tables = &table;
	p.profile.table_count = 1;
	p.files[3].name = "music/t.wav"; /* sorted after a/two.dat */
	p.files[3].size = (uint32_t)wav_size;
	p.files[3].sha256 = hex_of(T, wav, wav_size);
	p.profile.file_count = 4;
	pack = dir_new(T);
	core[0] = KP_RES_LISTING;
	core[1] = listing(T, "test-1", "group koprulu-music.mpq music\\\n", CORE);
	core[2] = CORE[0];
	core[3] = CORE[1];
	core[4] = NULL;
	dir_mpq(T, pack, "koprulu-core.mpq", core);
	dirs[0] = pack->dir;
	r = open_res(T, &p, dirs, 1);
	kp_res_files(r, &files);
	memset(&d, 0, sizeof d);
	e.msg[0] = 0;
	kp_t_eq(T, kp_ui_data_music_lengths(&d, &files, &e), 0, "%s", e.msg);
	kp_t_eq(T, (int64_t)d.music_lengths.size, 1 + 8 * 2, "MUSL: a count, 8 bytes a track");
	kp_t_eq(T, kp_le_u32(d.music_lengths.data + 1 + 8), 0, "no group: no length");
	for (k = 0; k < 44; k++) snprintf(hex_head + 2 * k, 3, "%02x", wav[k]);
	snprintf(text, sizeof text, "%s\nprofile test-1\n%s %u exact ogg %s %u %s music\\t.wav\n", KP_RES_LISTING_MAGIC,
		hex_of(T, OGG, strlen(OGG)), (unsigned)strlen(OGG), p.files[3].sha256, (unsigned)wav_size, hex_head);
	group[0] = KP_RES_LISTING;
	group[1] = kp_t_strdup(T, text);
	group[2] = "music\\t.wav";
	group[3] = OGG;
	group[4] = NULL;
	dir_mpq(T, pack, "koprulu-music.mpq", group);
	snprintf(path, sizeof path, "%s/koprulu-music.mpq", pack->dir);
	kp_t_eq(T, kp_res_add_repack(r, path, err, sizeof err), 0, "%s", err);
	kp_t_eq(T, kp_ui_data_music_lengths(&d, &files, &e), 0, "%s", e.msg);
	kp_t_eq(T, kp_le_u32(d.music_lengths.data + 1 + 8), 2000, "the group's track: its data length");
	kp_t_eq(T, kp_le_u32(d.music_lengths.data + 5 + 8), 44100, "its bytes a second");
	kp_ui_data_free(&d);
}

static void res_streams_read_pieces(KpT *T)
{
	static const char *const LISTED[] = { "a\\two.dat", TWO, NULL };
	const char *repack[5], *dirs[2];
	Profile p;
	Dir *install, *pack;
	KpRes *r;
	KpResStream *s;
	uint64_t size = 0;
	char err[512], piece[16];
	profile_init(T, &p);
	install = test_install(T);
	pack = dir_new(T);
	repack[0] = KP_RES_LISTING;
	repack[1] = listing(T, "test-1", "", LISTED);
	repack[2] = "a\\two.dat";
	repack[3] = TWO;
	repack[4] = NULL;
	dir_mpq(T, pack, "koprulu.mpq", repack);
	dirs[0] = install->dir;
	dirs[1] = pack->dir;
	r = open_res(T, &p, dirs, 2);
	s = kp_res_stream_open(r, "a/one.dat", &size, err, sizeof err);
	kp_t_ok(T, s != NULL, "%s", err);
	kp_t_eq(T, (int64_t)size, (int64_t)strlen(ONE), "one's size");
	memset(piece, 0, sizeof piece);
	kp_t_eq(T, kp_res_stream_read(s, 4, piece, 5), 0, "a piece");
	kp_t_eq_str(T, piece, "first", "one's bytes 4..8");
	kp_t_eq(T, kp_res_stream_read(s, size - 3, piece, 4), -1, "past the end");
	kp_res_stream_close(s);
	s = kp_res_stream_open(r, "a/two.dat", &size, err, sizeof err);
	kp_t_ok(T, s != NULL, "%s", err);
	kp_t_eq(T, (int64_t)size, (int64_t)strlen(TWO), "two from the repack");
	kp_res_stream_close(s);
	kp_t_ok(T, kp_res_stream_open(r, "a/gone.dat", &size, err, sizeof err) == NULL && !err[0], "an absent file: %s", err);
}

/* Archives laid in one file at offsets of their own, as an APK holds the repack's groups (stored, between other
 * entries' bytes), and KpResConfig.repack_file over it: a file's slice by name. */
typedef struct {
	int fd;
	const char *files[3];
	uint64_t offset[3], length[3];
	int count;
	const char *refuse; /* a file whose slice cannot be given (-1) */
} Bundle;

static void fd_close(void *p) { close(*(int *)p); }

static int bundle_file(void *ctx, const char *file, char *out, size_t size)
{
	Bundle *b = (Bundle *)ctx;
	int k;
	if (b->refuse && strcmp(file, b->refuse) == 0) {
		snprintf(out, size, "%s is compressed", file);
		return -1;
	}
	for (k = 0; k < b->count; k++)
		if (strcmp(b->files[k], file) == 0) return kp_mpq_slice_name(b->fd, b->offset[k], b->length[k], out, size) == 0 ? 1 : -1;
	return 0;
}

/* d's archives (written there) laid into d's bundle.apk after filler bytes of their own, each at its offset */
static Bundle *bundle_new(KpT *T, Dir *d, const char *const *files, int count)
{
	Bundle *b = (Bundle *)calloc(1, sizeof *b);
	const char *path = dir_path(T, d, "bundle.apk");
	FILE *out;
	uint64_t at = 0;
	int k;
	kp_t_ok(T, b != NULL, "out of memory");
	kp_t_defer(T, free, b); /* after the descriptor's close (the cleanups run last first) */
	kp_t_ok(T, (out = fopen(path, "wb")) != NULL, "cannot write %s", path);
	for (k = 0; k < count; k++) {
		char archive[320], filler[100];
		size_t size = 0, pad = 30 + 7 * (size_t)k; /* a zip entry's header and name before it, no alignment */
		const uint8_t *bytes;
		snprintf(archive, sizeof archive, "%s/%s", d->dir, files[k]);
		bytes = kp_t_read(T, archive, &size);
		memset(filler, 'P' + k, sizeof filler);
		kp_t_ok(T, fwrite(filler, 1, pad, out) == pad && fwrite(bytes, 1, size, out) == size, "cannot write %s", path);
		b->files[k] = files[k];
		b->offset[k] = at + pad;
		b->length[k] = size;
		at += pad + size;
	}
	kp_t_ok(T, fwrite("PK tail", 1, 7, out) == 7 && fclose(out) == 0, "cannot write %s", path);
	b->count = count;
	kp_t_ok(T, (b->fd = open(path, O_RDONLY)) >= 0, "cannot open %s: %s", path, strerror(errno));
	kp_t_defer(T, fd_close, &b->fd);
	return b;
}

static KpRes *open_res_bundle(KpT *T, Profile *p, const char *const *dirs, int count, Bundle *b, char *err, size_t err_size)
{
	KpResConfig c;
	KpRes *r;
	memset(&c, 0, sizeof c);
	c.profile = &p->profile;
	c.log = profile_log;
	c.log_ctx = p;
	c.repack_file = bundle_file;
	c.repack_ctx = b;
	if ((r = kp_res_open(dirs, count, &c, err, err_size)) != NULL) kp_t_defer(T, res_close, r);
	return r;
}

/* The slices of an open file are StormLib's POSIX file provider's (tools/toolchain/patches/StormLib-9.40): the Android
 * APK's groups; Windows has none to read. */
static void need_slices(KpT *T)
{
#ifdef _WIN32
	kp_t_skip(T, "an APK's slices are Android's (StormLib's POSIX file provider)");
#else
	(void)T;
#endif
}

static void res_repack_read_in_place_from_slices(KpT *T)
{
	static const char *const CORE[] = { "a\\one.dat", ONE, NULL };
	static const char *const GROUP[] = { "a\\two.dat", TWO, NULL };
	static const char *const FILES[] = { "koprulu-core.mpq", "koprulu-g1.mpq" };
	const char *core[5], *group[5];
	Profile p;
	Dir *pack;
	Bundle *b;
	KpRes *r;
	KpResStream *s;
	KpResStatus st;
	uint64_t size = 0;
	char err[512], file[128], piece[8];
	int counts[KP_RES_STATUS_COUNT];
	need_slices(T);
	profile_init(T, &p);
	pack = dir_new(T);
	core[0] = KP_RES_LISTING;
	core[1] = listing(T, "test-1", "group koprulu-g1.mpq a/two\ngroup koprulu-g2.mpq b/\n", CORE);
	core[2] = "a\\one.dat";
	core[3] = ONE;
	core[4] = NULL;
	group[0] = KP_RES_LISTING;
	group[1] = listing(T, "test-1", "", GROUP);
	group[2] = "a\\two.dat";
	group[3] = TWO;
	group[4] = NULL;
	dir_mpq(T, pack, FILES[0], core);
	dir_mpq(T, pack, FILES[1], group);
	b = bundle_new(T, pack, FILES, 2);
	r = open_res_bundle(T, &p, NULL, 0, b, err, sizeof err);
	kp_t_ok(T, r != NULL, "%s", err);
	kp_t_eq(T, kp_res_repacks(r), 2, "the core and its group from the bundle (g2 is not there)");
	kp_t_eq_str(T, kp_res_repack_dir(r), "", "no folder");
	kp_t_ok(T, strstr(p.log, "res: repack koprulu-core.mpq (fd:") != NULL, "logged by name: %s", p.log);
	kp_t_eq_str(T, read_text(T, r, "a/one.dat", &st, err, sizeof err), ONE, "the core's file");
	kp_t_eq(T, st, KP_RES_REPACKED, "one's status");
	kp_t_eq_str(T, read_text(T, r, "a/two.dat", &st, err, sizeof err), TWO, "the group's file");
	kp_t_eq(T, kp_res_group_wanted(r, "b\\x.dat", file, sizeof file), 1, "the group not in the bundle");
	kp_t_eq_str(T, file, "koprulu-g2.mpq", NULL);
	/* a stream opens the slice again, a descriptor of its own */
	s = kp_res_stream_open(r, "a/two.dat", &size, err, sizeof err);
	kp_t_ok(T, s != NULL, "%s", err);
	kp_t_eq(T, (int64_t)size, (int64_t)strlen(TWO), "two's size");
	memset(piece, 0, sizeof piece);
	kp_t_eq(T, kp_res_stream_read(s, 4, piece, 2), 0, "a piece");
	kp_t_eq_str(T, piece, "as", "two's bytes 4..5");
	kp_res_stream_close(s);
	kp_res_counts(r, counts);
	kp_t_eq(T, counts[KP_RES_REPACKED], 2, "repacked: one and two");
}

/* A slice is its archive, within its file; one that cannot be given leaves the install */
static void res_repack_slice_refused(KpT *T)
{
	static const char *const CORE[] = { "a\\one.dat", ONE, NULL };
	static const char *const FILES[] = { "koprulu-core.mpq" };
	const char *core[5], *dirs[1];
	Profile p;
	Dir *pack, *install;
	Bundle *b;
	KpRes *r;
	KpMpq *m;
	KpResStatus st;
	char err[512], name[128];
	need_slices(T);
	profile_init(T, &p);
	pack = dir_new(T);
	core[0] = KP_RES_LISTING;
	core[1] = listing(T, "test-1", "", CORE);
	core[2] = "a\\one.dat";
	core[3] = ONE;
	core[4] = NULL;
	dir_mpq(T, pack, FILES[0], core);
	b = bundle_new(T, pack, FILES, 1);
	kp_t_eq(T, kp_mpq_slice_name(b->fd, b->offset[0], b->length[0], name, sizeof name), 0, "named");
	m = kp_mpq_open(name, 0, err, sizeof err);
	kp_t_ok(T, m != NULL, "the slice opens: %s", err);
	kp_mpq_close(m);
	kp_t_eq(T, kp_mpq_slice_name(b->fd, b->offset[0], b->length[0], name, 8), -1, "no room for the name");
	kp_mpq_slice_name(b->fd, b->offset[0], 16, name, sizeof name);
	kp_t_ok(T, kp_mpq_open(name, 0, err, sizeof err) == NULL, "a slice short of its archive's header (the file goes on)");
	kp_mpq_slice_name(b->fd, b->offset[0], b->length[0] + 64, name, sizeof name);
	kp_t_ok(T, kp_mpq_open(name, 0, err, sizeof err) == NULL, "a slice past its file's end");
	kp_mpq_slice_name(-1, b->offset[0], b->length[0], name, sizeof name);
	kp_t_ok(T, kp_mpq_open(name, 0, err, sizeof err) == NULL, "a descriptor not open");
	/* the program's repack refused: the install is read, else nothing is */
	b->refuse = FILES[0];
	kp_t_ok(T, open_res_bundle(T, &p, NULL, 0, b, err, sizeof err) == NULL, "no source");
	kp_t_ok(T, strstr(err, "the program's repack") != NULL, "%s", err);
	kp_t_ok(T, strstr(p.log, "koprulu-core.mpq is compressed (ignored)\n") != NULL, "logged with no install: %s", p.log);
	install = test_install(T);
	dirs[0] = install->dir;
	r = open_res_bundle(T, &p, dirs, 1, b, err, sizeof err);
	kp_t_ok(T, r != NULL, "%s", err);
	kp_t_ok(T, strstr(p.log, "koprulu-core.mpq is compressed (ignored: the install is read)\n") != NULL, "logged: %s", p.log);
	kp_t_eq(T, kp_res_repacks(r), 0, "no repack");
	kp_t_eq_str(T, read_text(T, r, "a/one.dat", &st, err, sizeof err), ONE, "the install's");
	kp_t_eq(T, st, KP_RES_EXACT, "one's status");
}

static void res_log_lists_the_reads(KpT *T)
{
	Profile p;
	Dir *d, *out;
	KpRes *r;
	KpResStatus st;
	char err[512];
	const char *dirs[1], *path;
	size_t size;
	uint8_t *text;
	profile_init(T, &p);
	d = test_install(T);
	out = dir_new(T);
	dirs[0] = d->dir;
	r = open_res(T, &p, dirs, 1);
	read_text(T, r, "a\\one.dat", &st, err, sizeof err);
	read_text(T, r, "a\\gone.dat", &st, err, sizeof err);
	read_text(T, r, "a\\extra.txt", &st, err, sizeof err);
	path = dir_path(T, out, "res.log");
	kp_t_eq(T, kp_res_write_log(r, path, err, sizeof err), 0, "%s", err);
	text = kp_t_read(T, path, &size);
	kp_t_eq_str(T, (const char *)text, "a/extra.txt\tunlisted\tB.mpq\na/gone.dat\tabsent\t\na/one.dat\texact\tA.mpq\n", "the log");
}

/* Remastered's tilesets: the 1.17 prefix of a grown file, its CV5 groups Remastered filled zeroed; .vx4 from .vx4ex. */
static void res_casc_rebuilds_the_tilesets(KpT *T)
{
	static uint8_t grown[1100 * 52], wide[16];
	uint8_t *b;
	size_t k;
	memset(grown, 7, sizeof grown);
	b = kp_res_casc_rebuild("tileset/desert.cv5", "SD/tileset/desert.cv5", grown, sizeof grown, 1024 * 52);
	kp_t_ok(T, b != NULL, "desert.cv5 rebuilt");
	kp_t_eq(T, b[769 * 52 + 51], 7, "the last group before the filled ones");
	kp_t_eq(T, b[770 * 52], 0, "the first filled group zeroed");
	kp_t_eq(T, b[1024 * 52 - 1], 0, "the last filled group zeroed");
	free(b);
	b = kp_res_casc_rebuild("tileset/jungle.cv5", "SD/tileset/jungle.cv5", grown, sizeof grown, 1024 * 52);
	kp_t_ok(T, b != NULL && b[1023 * 52] == 7, "another tileset's groups are kept");
	free(b);
	b = kp_res_casc_rebuild("tileset/jungle.vf4", "tileset/jungle.vf4", grown, 100, 64);
	kp_t_ok(T, b != NULL && b[63] == 7, "a VF4's prefix");
	free(b);
	kp_t_ok(T, kp_res_casc_rebuild("tileset/jungle.vf4", "tileset/jungle.vf4", grown, 10, 64) == NULL, "shorter than the 1.17 file");
	kp_t_ok(T, kp_res_casc_rebuild("unit/x.grp", "unit/x.grp", grown, 100, 64) == NULL, "no rule");
	for (k = 0; k < 4; k++) {
		wide[4 * k] = (uint8_t)(k + 1);
		wide[4 * k + 1] = 0x80;
		wide[4 * k + 2] = wide[4 * k + 3] = 0;
	}
	b = kp_res_casc_rebuild("tileset/jungle.vx4", "SD/tileset/jungle.vx4ex", wide, sizeof wide, 6);
	kp_t_ok(T, b != NULL && b[0] == 1 && b[1] == 0x80 && b[4] == 3 && b[5] == 0x80, ".vx4's 16-bit references");
	free(b);
	wide[6] = 1;
	kp_t_ok(T, kp_res_casc_rebuild("tileset/jungle.vx4", "SD/tileset/jungle.vx4ex", wide, sizeof wide, 8) == NULL,
		"a reference past 16 bits");
}

/* --- the real installs (KP_TEST_ORIGINAL, KP_TEST_SCR) --- */

static void res_reads_the_1_17_install(KpT *T)
{
	KpRes *r = kp_t_install(T, "KP_TEST_ORIGINAL");
	KpResStatus st;
	KpResStream *s;
	char err[1024], head[4];
	size_t size;
	uint64_t stream_size;
	uint8_t *b;
	kp_t_eq(T, kp_res_install(r), KP_RES_SOURCE_MPQ, "a 1.17 install");
	b = kp_res_read(r, "arr\\units.dat", &size, &st, err, sizeof err);
	kp_t_ok(T, b != NULL && st == KP_RES_EXACT, "units.dat: %s", err);
	kp_t_eq(T, (int64_t)size, 19876, "units.dat's size");
	free(b);
	s = kp_res_stream_open(r, "music\\terran1.wav", &stream_size, err, sizeof err);
	kp_t_ok(T, s != NULL, "the music: %s", err);
	kp_t_ok(T, kp_res_stream_read(s, 0, head, 4) == 0 && memcmp(head, "RIFF", 4) == 0, "a WAV");
	kp_t_ok(T, kp_res_stream_read(s, stream_size - 4, head, 4) == 0, "its last bytes");
	kp_res_stream_close(s);
}

static void res_reads_the_remastered_install(KpT *T)
{
	KpRes *r = kp_t_install(T, "KP_TEST_SCR");
	KpResStatus st;
	char err[1024];
	size_t size;
	uint8_t *b;
	kp_t_eq(T, kp_res_install(r), KP_RES_SOURCE_CASC, "a Remastered install");
	b = kp_res_read(r, "arr\\units.dat", &size, &st, err, sizeof err);
	kp_t_ok(T, b != NULL && st == KP_RES_EXACT && size == 19876, "units.dat: %s", err);
	free(b);
	b = kp_res_read(r, "tileset\\jungle.vx4", &size, &st, err, sizeof err);
	kp_t_ok(T, b != NULL && st == KP_RES_DERIVED, "jungle.vx4 from .vx4ex: %s (%s)", err, kp_res_status_name(st));
	free(b);
	b = kp_res_read(r, "tileset\\desert.cv5", &size, &st, err, sizeof err);
	kp_t_ok(T, b != NULL && st == KP_RES_DERIVED, "desert.cv5's 1.17 prefix: %s (%s)", err, kp_res_status_name(st));
	free(b);
	b = kp_res_read(r, "files\\font\\font8.fnt", &size, &st, err, sizeof err);
	kp_t_ok(T, b != NULL && st == KP_RES_EXACT, "a moved font: %s (%s)", err, kp_res_status_name(st));
	free(b);
	kp_t_eq(T, (int64_t)strlen(kp_res_text_locale(r)), 4, "its text locale: %s", kp_res_text_locale(r));
	snprintf(err, sizeof err, "locales/%s/Assets/Smk/starintr.srt", kp_res_text_locale(r));
	kp_t_ok(T, kp_res_casc_held(r, err), "it holds that language's files: %s", err);
	kp_t_ok(T, !kp_res_casc_held(r, "locales/xxXX/Assets/Smk/starintr.srt"), "not a name of the storage");
}

/* A .build.info's text locale: the active row's first Tags group with "text?" and a locale */
static void res_build_info_text_locale(KpT *T)
{
	static const struct {
		const char *text, *want;
	} CASES[] = {
		{ "Branch!STRING:0|Active!DEC:1|Tags!STRING:0\r\n"
		  "us|0|Windows enUS text?:Windows enUS speech?\r\n"
		  "\r\n"
		  "eu|1|OSX EU? deDE speech?:OSX EU? deDE text?:OSX EU? ruRU text?\r\n", "deDE" },
		{ "Tags!STRING:0|Active!DEC:1\nOSX text?:OSX frFR text? EU?\n", "frFR" }, /* a group without a locale */
		{ "Tags!STRING:0|Active!DEC:1\nOSX  ruRU\ttext?:x\nOSX deDE text?|0\n", "ruRU" }, /* no active row: the first */
		{ "Tags!STRING:0|Active!DEC:1\nOSX deDE speech?|1\n", "" },
		{ "Tags!STRING:0|Active!DEC:1\nOSX enus text? EnUS ENUS|1\n", "" },
		{ "Branch!STRING:0|Active!DEC:1\neu|1\n", "" }, /* no Tags */
		{ "", "" },
	};
	size_t k;
	char out[8];
	for (k = 0; k < sizeof CASES / sizeof CASES[0]; k++) {
		kp_res_build_info_text_locale(CASES[k].text, out, sizeof out);
		kp_t_eq_str(T, out, CASES[k].want, "case %d", (int)k);
	}
	kp_res_build_info_text_locale(CASES[0].text, out, 4);
	kp_t_eq_str(T, out, "", "no room for a locale");
}

static const KpTest TESTS[] = {
	{ "sha256_vectors", sha256_vectors, 0 },
	{ "sha256_pieces", sha256_pieces, 0 },
	{ "sha256_padding_edges", sha256_padding_edges, 0 },
	{ "sha256_hex_decodes", sha256_hex_decodes, 0 },
	{ "res_reads_a_1_17_install_checked", res_reads_a_1_17_install_checked, 0 },
	{ "res_damaged_copy_reads_its_head", res_damaged_copy_reads_its_head, 0 },
	{ "res_install_needs_its_core_archives", res_install_needs_its_core_archives, 0 },
	{ "res_hashes_only_what_the_simulation_reads", res_hashes_only_what_the_simulation_reads, 0 },
	{ "res_repack_comes_before_the_install", res_repack_comes_before_the_install, 0 },
	{ "res_repack_of_another_profile_is_ignored", res_repack_of_another_profile_is_ignored, 0 },
	{ "res_repack_without_its_profile_leaves_nothing", res_repack_without_its_profile_leaves_nothing, 0 },
	{ "res_repack_groups_join_the_core", res_repack_groups_join_the_core, 0 },
	{ "res_repack_listing_statuses", res_repack_listing_statuses, 0 },
	{ "res_repack_group_wanted_then_added", res_repack_group_wanted_then_added, 0 },
	{ "res_music_lengths_come_with_their_group", res_music_lengths_come_with_their_group, 0 },
	{ "res_streams_read_pieces", res_streams_read_pieces, 0 },
	{ "res_repack_read_in_place_from_slices", res_repack_read_in_place_from_slices, 0 },
	{ "res_repack_slice_refused", res_repack_slice_refused, 0 },
	{ "res_log_lists_the_reads", res_log_lists_the_reads, 0 },
	{ "res_casc_rebuilds_the_tilesets", res_casc_rebuilds_the_tilesets, 0 },
	{ "res_build_info_text_locale", res_build_info_text_locale, 0 },
	{ "res_reads_the_1_17_install", res_reads_the_1_17_install, 0 },
	{ "res_reads_the_remastered_install", res_reads_the_remastered_install, 0 },
};
KP_SUITE(kp_suite_res, "test_res", TESTS);
