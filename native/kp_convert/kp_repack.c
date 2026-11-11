#include "kp_repack.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_cine.h"
#include "kp_fs.h"
#include "kp_json.h"
#include "kp_mpq.h"
#include "kp_ogg.h"
#include "kp_sha256.h"
#include "kp_wav.h"
#include "kp_work.h"

#define SINGLE "koprulu.mpq"
#define CORE "koprulu-core.mpq"
#define MUSIC "koprulu-music.mpq"
#define CINEMATICS "koprulu-cinematics.mpq"
#define WINDOW 64 /* the files read, then encoded, at a time (the music's WAVs are 17 MB each) */
#define SPARE_FILES 8 /* an archive's room past its files: the listing, (listfile) */
#define PATH 1024
#define NOTE 400

/* A file to read: its name as the archive and the listing spell it, its normalized name, its movie (-1 none). */
typedef struct {
	char name[KP_REPACK_NAME], key[KP_REPACK_NAME];
	int movie;
} Read;

/* A file of the window: as read, then as stored. */
typedef struct {
	const Read *read;
	uint8_t *data;
	size_t size;
	KpResStatus status;
	uint8_t *ogg; /* the Ogg Vorbis that replaces it, NULL: stored as it is */
	size_t ogg_size;
	uint8_t wav_sha[KP_SHA256_SIZE];
	size_t header_size; /* the WAV's header, data[0, header_size) */
	int encoded;        /* the Ogg was encoded now (not the cache's) */
	char note[NOTE];    /* why a WAV stays a WAV, logged in order */
} Job;

typedef struct {
	char file[64], prefix[KP_REPACK_NAME];
	KpMpq *mpq;
	KpBuf lines; /* its listing's file lines */
	int planned, files;
	uint64_t bytes;
} Group;

typedef struct {
	const KpRepackConfig *c;
	Job *jobs;
} Encoding;

static void normalize(const char *name, char *out, size_t size)
{
	size_t i;
	for (i = 0; name[i] && i + 1 < size; i++) out[i] = name[i] == '\\' ? '/' : (char)tolower((unsigned char)name[i]);
	out[i] = 0;
}

static void mpq_name(const char *key, char *out, size_t size)
{
	size_t i;
	for (i = 0; key[i] && i + 1 < size; i++) out[i] = key[i] == '/' ? '\\' : key[i];
	out[i] = 0;
}

static int ends_with(const char *s, const char *suffix)
{
	size_t n = strlen(s), k = strlen(suffix);
	return n >= k && strcmp(s + n - k, suffix) == 0;
}

void kp_repack_group(const char *key, char *out, size_t size, char *prefix, size_t prefix_size)
{
	static const char CAMPAIGN[] = "campaign/";
	const char *at;
	if (strncmp(key, CAMPAIGN, sizeof CAMPAIGN - 1) == 0 && (at = strstr(key, "/staredit/")) != NULL
		&& at > key + sizeof CAMPAIGN - 1) {
		size_t n = (size_t)(at - key) + 1, i, o;
		snprintf(prefix, prefix_size, "%.*s", (int)n, key);
		o = (size_t)snprintf(out, size, "koprulu");
		/* "koprulu-" and the directories between "campaign/" and "/staredit/", '-' apart */
		for (i = sizeof CAMPAIGN - 2; i + 1 < n && o + 1 < size; i++) out[o++] = key[i] == '/' ? '-' : key[i];
		snprintf(out + o, size > o ? size - o : 0, ".mpq");
		return;
	}
	if (strncmp(key, "music/", 6) == 0) {
		snprintf(out, size, MUSIC);
		snprintf(prefix, prefix_size, "music/");
	} else if (strncmp(key, "smk/", 4) == 0) {
		snprintf(out, size, CINEMATICS);
		snprintf(prefix, prefix_size, "smk/");
	} else {
		snprintf(out, size, CORE);
		snprintf(prefix, prefix_size, "%s", "");
	}
}

/* The file's group in the repack: its own, or with single the root's (the cinematics' kept apart). */
static Group *group_of(Group *groups, int *count, const char *key, int single, char *err, size_t err_size)
{
	char file[64], prefix[KP_REPACK_NAME];
	int k;
	kp_repack_group(key, file, sizeof file, prefix, sizeof prefix);
	if (single && strcmp(file, CINEMATICS) != 0) {
		snprintf(file, sizeof file, SINGLE);
		prefix[0] = 0;
	}
	for (k = 0; k < *count; k++)
		if (strcmp(groups[k].file, file) == 0) return &groups[k];
	if (*count == KP_REPACK_GROUPS) {
		snprintf(err, err_size, "more than %d groups", KP_REPACK_GROUPS);
		return NULL;
	}
	memset(&groups[*count], 0, sizeof groups[0]);
	snprintf(groups[*count].file, sizeof groups[0].file, "%s", file);
	snprintf(groups[*count].prefix, sizeof groups[0].prefix, "%s", prefix);
	return &groups[(*count)++];
}

static int add_read(Read **reads, int *count, int *cap, const char *name, int movie)
{
	Read *r;
	if (*count == *cap) {
		int grown_cap = *cap ? 2 * *cap : 4096;
		Read *grown = (Read *)realloc(*reads, (size_t)grown_cap * sizeof *grown);
		if (!grown) return -1;
		*reads = grown;
		*cap = grown_cap;
	}
	r = &(*reads)[(*count)++];
	normalize(name, r->key, sizeof r->key);
	mpq_name(r->key, r->name, sizeof r->name);
	r->movie = movie;
	return 0;
}

static int listed(const Read *reads, int count, const char *key)
{
	int k;
	for (k = 0; k < count; k++)
		if (strcmp(reads[k].key, key) == 0) return 1;
	return 0;
}

/* The files to read, in order: the manifest's, the movies' (a 1.17 install's), the extra names the rest leave out. */
static Read *plan(KpRes *res, const KpRepackConfig *c, const KpCineCatalog *movies, int *count, char *err, size_t err_size)
{
	const KpDefsProfile *p = kp_res_profile(res);
	Read *reads = NULL;
	int cap = 0, k, a, manifest;
	*count = 0;
	for (k = 0; k < p->file_count; k++)
		if (add_read(&reads, count, &cap, p->files[k].name, -1) != 0) goto memory;
	for (k = 0; movies && k < movies->count; k++) {
		char name[KP_REPACK_NAME];
		snprintf(name, sizeof name, "smk\\%s.smk", movies->movies[k].name);
		if (add_read(&reads, count, &cap, name, k) != 0) goto memory;
	}
	manifest = *count;
	for (k = 0; k < c->extra_count; k++) {
		char key[KP_REPACK_NAME];
		normalize(c->extra[k], key, sizeof key);
		for (a = 0; a < p->absent_count; a++)
			if (strcmp(p->absent[a], key) == 0) break;
		if (a < p->absent_count || listed(reads, manifest, key) || listed(reads + manifest, *count - manifest, key)) continue;
		if (add_read(&reads, count, &cap, c->extra[k], -1) != 0) goto memory;
	}
	return reads;
memory:
	free(reads);
	snprintf(err, err_size, "out of memory");
	return NULL;
}

/* The Ogg Vorbis of a WAV from the cache, or encoded and kept there. */
static void encode(void *ctx, int k)
{
	Encoding *e = (Encoding *)ctx;
	Job *j = &e->jobs[k];
	KpWavInfo w;
	char hex[2 * KP_SHA256_SIZE + 1], path[PATH], why[NOTE];
	uint32_t serial;
	size_t size = 0;
	uint8_t *ogg = NULL;
	if (!j->data || !ends_with(j->read->key, ".wav")) return;
	if (kp_wav_info(j->data, j->size, j->size, &w) != 0 || w.data_offset > KP_REPACK_WAV_HEAD) return;
	kp_sha256(j->data, j->size, j->wav_sha);
	serial = kp_ogg_serial(j->read->key);
	if (e->c->cache) {
		kp_sha256_hex(j->wav_sha, hex);
		snprintf(path, sizeof path, "%s/%s-q%d-s%u.ogg", e->c->cache, hex, (int)(KP_OGG_QUALITY * 10.0f + 0.5f), serial);
		ogg = kp_fs_read(path, &size, why, sizeof why);
	}
	if (!ogg) {
		if (!(ogg = kp_ogg_from_wav(j->data, j->size, KP_OGG_QUALITY, serial, &size, why, sizeof why))) {
			snprintf(j->note, sizeof j->note, "%s: kept as a WAV (%s)", j->read->name, why);
			return;
		}
		j->encoded = 1;
		if (e->c->cache && kp_fs_write(path, ogg, size, why, sizeof why) != 0)
			snprintf(j->note, sizeof j->note, "%s: the Ogg Vorbis is not kept: %s", j->read->name, why);
	}
	if (size >= j->size) { /* not smaller: the WAV as it is */
		free(ogg);
		return;
	}
	j->ogg = ogg;
	j->ogg_size = size;
	j->header_size = w.data_offset;
}

static void report_name(char names[][KP_REPACK_NAME], int *count, const char *name)
{
	if (*count < KP_REPACK_NAMES) snprintf(names[*count], KP_REPACK_NAME, "%s", name);
	(*count)++;
}

static void say(const KpRepackConfig *c, const char *line)
{
	if (c->log && line[0]) c->log(c->log_ctx, line);
}

/* The job's file into its group's archive and listing. */
static int store(Group *g, Job *j, KpRepackReport *rep, char *err, size_t err_size)
{
	const uint8_t *data = j->ogg ? j->ogg : j->data;
	size_t size = j->ogg ? j->ogg_size : j->size;
	uint8_t sha[KP_SHA256_SIZE];
	char hex[2 * KP_SHA256_SIZE + 1];
	int compress = !j->ogg && !ends_with(j->read->key, ".smk") && !ends_with(j->read->key, ".ogg");
	if (kp_mpq_add(g->mpq, j->read->name, data, size, compress ? KP_MPQ_ZLIB : KP_MPQ_STORE, err, err_size) != 0) return -1;
	kp_sha256(data, size, sha);
	kp_sha256_hex(sha, hex);
	kp_buf_printf(&g->lines, "%s %lu %s", hex, (unsigned long)size, j->read->movie >= 0 ? "exact" : kp_res_status_name(j->status));
	if (j->ogg) {
		size_t i;
		kp_sha256_hex(j->wav_sha, hex);
		kp_buf_printf(&g->lines, " ogg %s %lu ", hex, (unsigned long)j->size);
		for (i = 0; i < j->header_size; i++) kp_buf_printf(&g->lines, "%02x", j->data[i]);
		rep->ogg++;
		rep->wav_bytes += j->size;
		rep->ogg_bytes += size;
	}
	kp_buf_printf(&g->lines, " %s\n", j->read->name);
	g->files++;
	g->bytes += size;
	rep->statuses[j->read->movie >= 0 ? KP_RES_EXACT : j->status]++;
	if (j->encoded) rep->encoded++;
	return 0;
}

static int cmp_groups(const void *a, const void *b)
{
	return strcmp((*(const Group *const *)a)->file, (*(const Group *const *)b)->file);
}

/* Each archive's listing written and the archive closed; the root's names the groups that hold files. */
static int finish(Group *groups, int count, const char *root, const char *head, const char *out, KpRepackReport *rep,
	char *err, size_t err_size)
{
	Group *sorted[KP_REPACK_GROUPS];
	int k, failed = 0;
	for (k = 0; k < count; k++) sorted[k] = &groups[k];
	qsort(sorted, (size_t)count, sizeof sorted[0], cmp_groups);
	for (k = 0; k < count; k++) {
		Group *g = &groups[k];
		KpBuf listing;
		int i;
		kp_buf_init(&listing);
		kp_buf_str(&listing, head);
		if (strcmp(g->file, root) == 0)
			for (i = 0; i < count; i++)
				if (sorted[i] != g && sorted[i]->files) kp_buf_printf(&listing, "group %s %s\n", sorted[i]->file, sorted[i]->prefix);
		kp_buf_add(&listing, g->lines.data, g->lines.size);
		if (!failed && kp_mpq_add(g->mpq, KP_RES_LISTING, listing.data, listing.size, KP_MPQ_ZLIB, err, err_size) != 0)
			failed = 1;
		kp_buf_free(&listing);
	}
	for (k = 0; k < count; k++) {
		Group *g = &groups[k];
		char path[PATH];
		if (kp_mpq_close(g->mpq) != 0 && !failed) {
			snprintf(err, err_size, "cannot write %s/%s", out, g->file);
			failed = 1;
		}
		g->mpq = NULL;
		snprintf(path, sizeof path, "%s/%s", out, g->file);
		if (!g->files && strcmp(g->file, root) != 0) {
			remove(path);
			continue;
		}
		if (rep->group_count < KP_REPACK_GROUPS) {
			snprintf(rep->groups[rep->group_count].file, sizeof rep->groups[0].file, "%s", g->file);
			rep->groups[rep->group_count].files = g->files;
			rep->groups[rep->group_count].bytes = g->bytes;
			rep->group_count++;
		}
	}
	return failed ? -1 : 0;
}

int kp_repack_write(KpRes *res, const KpRepackConfig *c, KpRepackReport *rep, char *err, size_t err_size)
{
	const KpDefsProfile *p = kp_res_profile(res);
	KpResSourceKind kind = kp_res_install(res);
	const char *root = c->single ? SINGLE : CORE;
	KpCineCatalog movies, *have_movies = NULL;
	Group *groups = NULL;
	Read *reads = NULL;
	Job *jobs = NULL;
	KpBuf head;
	int count = 0, group_count = 0, k, at, rc = -1, threads = c->threads > 0 ? c->threads : kp_work_processors();
	memset(rep, 0, sizeof *rep);
	kp_buf_init(&head);
	if (kp_res_repacks(res) || (kind != KP_RES_SOURCE_MPQ && kind != KP_RES_SOURCE_CASC)) {
		snprintf(err, err_size, "the repack is made from an install (1.17's or Remastered's), not from %s",
			kp_res_repacks(res) ? "a repack" : "nothing");
		return -1;
	}
	if (kind == KP_RES_SOURCE_MPQ) { /* the movies the runtime plays as they are; Remastered's are its WebM */
		const KpDefsTable *t = kp_defs_table(p, "CINM");
		if (!t || kp_cine_catalog(&movies, t->data, t->size) != 0) {
			snprintf(err, err_size, "the profile's movies (CINM) do not read");
			return -1;
		}
		have_movies = &movies;
	}
	if (!(reads = plan(res, c, have_movies, &count, err, err_size))) return -1;
	if (!(groups = (Group *)calloc(KP_REPACK_GROUPS, sizeof *groups)) || !(jobs = (Job *)calloc(WINDOW, sizeof *jobs))) {
		snprintf(err, err_size, "out of memory");
		goto done;
	}
	for (k = 0; k < count; k++) {
		Group *g = group_of(groups, &group_count, reads[k].key, c->single, err, err_size);
		if (!g) goto done;
		g->planned++;
	}
	if (!group_of(groups, &group_count, "", c->single, err, err_size)) goto done; /* the root, even with no file */
	if (kp_fs_mkdirs(c->out, err, err_size) != 0 || kp_fs_remove_matching(c->out, "koprulu", ".mpq", err, err_size) < 0)
		goto done;
	if (c->cache && kp_fs_mkdirs(c->cache, err, err_size) != 0) goto done;
	for (k = 0; k < group_count; k++) {
		char path[PATH];
		snprintf(path, sizeof path, "%s/%s", c->out, groups[k].file);
		if (!(groups[k].mpq = kp_mpq_create(path, (uint32_t)(groups[k].planned + SPARE_FILES), err, err_size))) goto done;
	}
	kp_buf_printf(&head, "%s\nprofile %s\nsource %s\n", KP_RES_LISTING_MAGIC, p->id, kind == KP_RES_SOURCE_MPQ ? "1.17" : "remastered");
	for (at = 0; at < count; at += WINDOW) {
		int n = count - at < WINDOW ? count - at : WINDOW;
		Encoding e;
		for (k = 0; k < n; k++) { /* read in order on this thread: a KpRes is one thread's */
			Job *j = &jobs[k];
			char why[1400];
			memset(j, 0, sizeof *j);
			j->read = &reads[at + k];
			j->data = kp_res_read(res, j->read->name, &j->size, &j->status, why, sizeof why);
			if (!j->data) {
				if (j->status == KP_RES_REFUSED) {
					snprintf(err, err_size, "%s", why);
					goto done;
				}
				if (why[0]) report_name(rep->damaged_names, &rep->damaged, j->read->name);
				else report_name(rep->missing_names, &rep->missing, j->read->name);
				continue;
			}
			if (j->read->movie >= 0) {
				uint8_t sha[KP_SHA256_SIZE];
				kp_sha256(j->data, j->size, sha);
				if (j->size != movies.movies[j->read->movie].size || memcmp(sha, movies.movies[j->read->movie].sha256, sizeof sha) != 0) {
					snprintf(why, sizeof why, "%s: not the reference copy's bytes, left out", j->read->name);
					say(c, why);
					free(j->data);
					j->data = NULL;
					report_name(rep->missing_names, &rep->missing, j->read->name);
				}
			}
		}
		if (!c->wav) {
			e.c = c;
			e.jobs = jobs;
			kp_work_run(encode, &e, n, threads);
		}
		for (k = 0; k < n; k++) {
			Job *j = &jobs[k];
			Group *g;
			say(c, j->note);
			if (!j->data) continue;
			g = group_of(groups, &group_count, j->read->key, c->single, err, err_size);
			if (!g || store(g, j, rep, err, err_size) != 0) goto done;
			free(j->data);
			free(j->ogg);
			j->data = j->ogg = NULL;
		}
	}
	rc = finish(groups, group_count, root, kp_buf_text(&head), c->out, rep, err, err_size);
done:
	for (k = 0; jobs && k < WINDOW; k++) {
		free(jobs[k].data);
		free(jobs[k].ogg);
	}
	for (k = 0; groups && k < group_count; k++) {
		if (groups[k].mpq) kp_mpq_close(groups[k].mpq);
		kp_buf_free(&groups[k].lines);
	}
	kp_buf_free(&head);
	free(jobs);
	free(groups);
	free(reads);
	return rc;
}
