#include "kp_casc.h"

#include <stdio.h>
#include <stdlib.h>

#include "CascLib.h"

struct KpCasc {
	HANDLE storage;
};

static void reason(char *err, size_t err_size, const char *what, const char *name)
{
	if (err_size) snprintf(err, err_size, "%s %s: CascLib error %u", what, name, (unsigned)GetCascError());
}

KpCasc *kp_casc_open(const char *root, char *err, size_t err_size)
{
	HANDLE storage = NULL;
	if (!CascOpenStorage(root, 0, &storage)) {
		reason(err, err_size, "cannot open the CASC storage of", root);
		return NULL;
	}
	KpCasc *c = static_cast<KpCasc *>(malloc(sizeof(KpCasc)));
	if (!c) {
		CascCloseStorage(storage);
		if (err_size) snprintf(err, err_size, "out of memory");
		return NULL;
	}
	c->storage = storage;
	return c;
}

void kp_casc_close(KpCasc *c)
{
	if (!c) return;
	CascCloseStorage(c->storage);
	free(c);
}

int kp_casc_has(KpCasc *c, const char *name)
{
	HANDLE f = NULL;
	if (!CascOpenFile(c->storage, name, CASC_LOCALE_ALL, CASC_OPEN_BY_NAME, &f)) return 0;
	CascCloseFile(f);
	return 1;
}

uint8_t *kp_casc_read(KpCasc *c, const char *name, size_t *size, char *err, size_t err_size)
{
	HANDLE f = NULL;
	ULONGLONG n = 0;
	DWORD got = 0;
	if (err_size) err[0] = 0;
	if (!CascOpenFile(c->storage, name, CASC_LOCALE_ALL, CASC_OPEN_BY_NAME, &f)) {
		if (GetCascError() != ERROR_FILE_NOT_FOUND) reason(err, err_size, "cannot open", name);
		return NULL;
	}
	if (!CascGetFileSize64(f, &n) || n > 0x7fffffffu) {
		reason(err, err_size, "cannot size", name);
		CascCloseFile(f);
		return NULL;
	}
	uint8_t *b = static_cast<uint8_t *>(malloc(static_cast<size_t>(n) + 1));
	if (!b) {
		if (err_size) snprintf(err, err_size, "out of memory for %s", name);
		CascCloseFile(f);
		return NULL;
	}
	if (!CascReadFile(f, b, static_cast<DWORD>(n), &got) || got != n) {
		reason(err, err_size, "cannot read", name);
		free(b);
		CascCloseFile(f);
		return NULL;
	}
	CascCloseFile(f);
	b[n] = 0;
	*size = static_cast<size_t>(n);
	return b;
}
