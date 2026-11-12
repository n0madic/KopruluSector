/* MPQ archives through StormLib (kp_mpq.h). */
#include "kp_mpq.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* last and alone: on Windows it includes windows.h (kp_mpq.h) */
#include "StormLib.h"

struct KpMpq {
	HANDLE h;
	int created;
};

struct KpMpqFile {
	HANDLE h;
	uint64_t size;
};

static void reason(char *err, size_t err_size, const char *what, const char *name)
{
	if (err_size) snprintf(err, err_size, "%s %s: StormLib error %u", what, name, (unsigned)SErrGetLastError());
}

static KpMpq *wrap(HANDLE h, int created, char *err, size_t err_size)
{
	KpMpq *m = (KpMpq *)malloc(sizeof *m);
	if (!m) {
		SFileCloseArchive(h);
		if (err_size) snprintf(err, err_size, "out of memory");
		return NULL;
	}
	m->h = h;
	m->created = created;
	return m;
}

/* No (attributes): the files are checked against SHA-256 digests (the input manifest, a repack's listing), and
 * StormLib reads and writes its 64-bit file times at unaligned addresses (SFileAttributes.cpp, undefined behaviour the
 * sanitizers stop on). The sectors' CRCs do not depend on it. */
KpMpq *kp_mpq_open(const char *path, int check_crc, char *err, size_t err_size)
{
	HANDLE h = NULL;
	DWORD flags = STREAM_FLAG_READ_ONLY | MPQ_OPEN_NO_ATTRIBUTES | (check_crc ? MPQ_OPEN_CHECK_SECTOR_CRC : 0);
	if (!SFileOpenArchive(path, 0, flags, &h)) {
		reason(err, err_size, "cannot open the archive", path);
		return NULL;
	}
	return wrap(h, 0, err, err_size);
}

int kp_mpq_slice_name(int fd, uint64_t offset, uint64_t length, char *out, size_t size)
{
	int n = snprintf(out, size, "fd:%d:%llu:%llu", fd, (unsigned long long)offset, (unsigned long long)length);
	return n > 0 && (size_t)n < size ? 0 : -1;
}

KpMpq *kp_mpq_create(const char *path, uint32_t max_files, char *err, size_t err_size)
{
	HANDLE h = NULL;
	remove(path); /* SFileCreateArchive refuses an existing file */
	if (!SFileCreateArchive(path, MPQ_CREATE_ARCHIVE_V1 | MPQ_CREATE_LISTFILE,
		max_files < 16 ? 16 : max_files, &h)) {
		reason(err, err_size, "cannot create the archive", path);
		return NULL;
	}
	return wrap(h, 1, err, err_size);
}

int kp_mpq_add(KpMpq *m, const char *name, const void *data, size_t size, int compression, char *err, size_t err_size)
{
	HANDLE f = NULL;
	DWORD how = compression == KP_MPQ_BZIP2 ? MPQ_COMPRESSION_BZIP2 : MPQ_COMPRESSION_ZLIB;
	DWORD flags = MPQ_FILE_REPLACEEXISTING | (compression == KP_MPQ_STORE ? 0 : MPQ_FILE_COMPRESS);
	if (size > 0xffffffffu) {
		if (err_size) snprintf(err, err_size, "%s: too large for an MPQ", name);
		return -1;
	}
	if (!SFileCreateFile(m->h, name, 0, (DWORD)size, 0, flags, &f)) {
		reason(err, err_size, "cannot add", name);
		return -1;
	}
	if (size && !SFileWriteFile(f, data, (DWORD)size, how)) {
		reason(err, err_size, "cannot write", name);
		SFileFinishFile(f);
		return -1;
	}
	if (!SFileFinishFile(f)) {
		reason(err, err_size, "cannot finish", name);
		return -1;
	}
	return 0;
}

int kp_mpq_stored_at(KpMpq *m, const char *name, uint64_t *offset)
{
	const DWORD OTHERWISE = MPQ_FILE_COMPRESS_MASK | MPQ_FILE_ENCRYPTED | MPQ_FILE_SECTOR_CRC | MPQ_FILE_PATCH_FILE;
	HANDLE f = NULL;
	ULONGLONG at = 0, base = 0;
	DWORD flags = 0;
	int ok;
	if (!SFileOpenFileEx(m->h, name, SFILE_OPEN_FROM_MPQ, &f)) return -1;
	ok = SFileGetFileInfo(f, SFileInfoFlags, &flags, sizeof flags, NULL) && SFileGetFileInfo(f, SFileInfoByteOffset, &at, sizeof at, NULL)
		&& SFileGetFileInfo(m->h, SFileMpqHeaderOffset, &base, sizeof base, NULL) && !(flags & OTHERWISE);
	SFileCloseFile(f);
	if (!ok) return -1;
	*offset = (uint64_t)(base + at);
	return 0;
}

int kp_mpq_close(KpMpq *m)
{
	int rc = 0;
	if (!m) return 0;
	if (m->created && !SFileFlushArchive(m->h)) rc = -1;
	if (!SFileCloseArchive(m->h)) rc = -1;
	free(m);
	return rc;
}

int kp_mpq_has(KpMpq *m, const char *name) { return SFileHasFile(m->h, name) ? 1 : 0; }

uint8_t *kp_mpq_read(KpMpq *m, const char *name, size_t *size, char *err, size_t err_size)
{
	HANDLE f = NULL;
	DWORD high = 0, n, got = 0;
	uint8_t *b;
	if (err_size) err[0] = 0;
	*size = 0;
	if (!SFileOpenFileEx(m->h, name, SFILE_OPEN_FROM_MPQ, &f)) {
		if (SErrGetLastError() != ERROR_FILE_NOT_FOUND) reason(err, err_size, "cannot open", name);
		return NULL;
	}
	n = SFileGetFileSize(f, &high);
	if (n == SFILE_INVALID_SIZE || high) {
		reason(err, err_size, "cannot size", name);
		SFileCloseFile(f);
		return NULL;
	}
	if (!(b = (uint8_t *)malloc((size_t)n + 1))) {
		if (err_size) snprintf(err, err_size, "out of memory for %s", name);
		SFileCloseFile(f);
		return NULL;
	}
	if ((n && !SFileReadFile(f, b, n, &got, NULL)) || got != n) {
		reason(err, err_size, "cannot read", name);
		free(b);
		SFileCloseFile(f);
		return NULL;
	}
	SFileCloseFile(f);
	b[n] = 0;
	*size = n;
	return b;
}

KpMpqFile *kp_mpq_file_open(KpMpq *m, const char *name, uint64_t *size, char *err, size_t err_size)
{
	HANDLE h = NULL;
	DWORD high = 0, n;
	KpMpqFile *f;
	if (err_size) err[0] = 0;
	if (!SFileOpenFileEx(m->h, name, SFILE_OPEN_FROM_MPQ, &h)) {
		if (SErrGetLastError() != ERROR_FILE_NOT_FOUND) reason(err, err_size, "cannot open", name);
		return NULL;
	}
	n = SFileGetFileSize(h, &high);
	if (n == SFILE_INVALID_SIZE || !(f = (KpMpqFile *)malloc(sizeof *f))) {
		reason(err, err_size, "cannot size", name);
		SFileCloseFile(h);
		return NULL;
	}
	f->h = h;
	f->size = (uint64_t)high << 32 | n;
	*size = f->size;
	return f;
}

int kp_mpq_file_read(KpMpqFile *f, uint64_t offset, void *out, size_t size)
{
	LONG high = (LONG)(offset >> 32);
	DWORD got = 0;
	if (offset > f->size || size > f->size - offset || size > 0xffffffffu) return -1;
	if (SFileSetFilePointer(f->h, (LONG)(uint32_t)offset, &high, FILE_BEGIN) == SFILE_INVALID_POS) return -1;
	if (size && (!SFileReadFile(f->h, out, (DWORD)size, &got, NULL) || got != size)) return -1;
	return 0;
}

void kp_mpq_file_close(KpMpqFile *f)
{
	if (!f) return;
	SFileCloseFile(f->h);
	free(f);
}
