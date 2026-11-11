#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "kp_fs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define KP_FS_PATH 1024

static int make_dir(const char *path)
{
#ifdef _WIN32
	return _mkdir(path);
#else
	return mkdir(path, 0777);
#endif
}

int kp_fs_mkdirs(const char *path, char *err, size_t err_size)
{
	char p[KP_FS_PATH];
	size_t i, n = strlen(path);
	if (n == 0) return 0;
	if (n >= sizeof p) {
		snprintf(err, err_size, "%s: the path is too long", path);
		return -1;
	}
	memcpy(p, path, n + 1);
	for (i = 1; i <= n; i++) {
		if (p[i] != '/' && p[i] != '\\' && p[i] != 0) continue;
		if (i > 0 && p[i - 1] == ':') continue; /* a drive, "C:\" */
		{
			char c = p[i];
			p[i] = 0;
			if (make_dir(p) != 0 && errno != EEXIST) {
				snprintf(err, err_size, "cannot make %s: %s", p, strerror(errno));
				return -1;
			}
			p[i] = c;
		}
	}
	return 0;
}

uint8_t *kp_fs_read(const char *path, size_t *size, char *err, size_t err_size)
{
	FILE *f = fopen(path, "rb");
	uint8_t *b = NULL;
	long n;
	if (err_size) err[0] = 0;
	if (!f) {
		if (errno != ENOENT) snprintf(err, err_size, "cannot read %s: %s", path, strerror(errno));
		return NULL;
	}
	if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0
		|| !(b = (uint8_t *)malloc((size_t)n + 1)) || fread(b, 1, (size_t)n, f) != (size_t)n) {
		snprintf(err, err_size, "cannot read %s", path);
		free(b);
		fclose(f);
		return NULL;
	}
	fclose(f);
	b[n] = 0;
	*size = (size_t)n;
	return b;
}

int kp_fs_write(const char *path, const void *data, size_t size, char *err, size_t err_size)
{
	char tmp[KP_FS_PATH];
	FILE *f;
	int ok;
	/* apart from another thread's file of the same name: the address of a local, on that call's own stack */
	snprintf(tmp, sizeof tmp, "%s.%lx.tmp", path, (unsigned long)(uintptr_t)&f);
	if (!(f = fopen(tmp, "wb"))) {
		snprintf(err, err_size, "cannot write %s: %s", tmp, strerror(errno));
		return -1;
	}
	ok = fwrite(data, 1, size, f) == size;
	ok = fclose(f) == 0 && ok;
#ifdef _WIN32
	ok = ok && MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING);
#else
	ok = ok && rename(tmp, path) == 0;
#endif
	if (!ok) {
		remove(tmp);
		snprintf(err, err_size, "cannot write %s", path);
		return -1;
	}
	return 0;
}

int kp_fs_write_if_changed(const char *path, const void *data, size_t size, char *err, size_t err_size)
{
	size_t had = 0;
	uint8_t *old = kp_fs_read(path, &had, err, err_size);
	int same = old && had == size && memcmp(old, data, size) == 0;
	free(old);
	if (same) return 0;
	return kp_fs_write(path, data, size, err, err_size) == 0 ? 1 : -1;
}

static int matches(const char *name, const char *prefix, const char *suffix)
{
	size_t n = strlen(name), p = strlen(prefix), s = strlen(suffix);
	return n >= p + s && strncmp(name, prefix, p) == 0 && strcmp(name + n - s, suffix) == 0;
}

static int cmp_names(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int add_name(char ***names, int *count, int *cap, const char *name)
{
	char *copy;
	if (*count == *cap) {
		int grown_cap = *cap ? 2 * *cap : 64;
		char **grown = (char **)realloc(*names, (size_t)grown_cap * sizeof *grown);
		if (!grown) return -1;
		*names = grown;
		*cap = grown_cap;
	}
	if (!(copy = (char *)malloc(strlen(name) + 1))) return -1;
	strcpy(copy, name);
	(*names)[(*count)++] = copy;
	return 0;
}

char **kp_fs_list(const char *dir, const char *suffix, int *count, char *err, size_t err_size)
{
	char **names = NULL;
	int cap = 0, failed = 0;
#ifdef _WIN32
	char pattern[KP_FS_PATH];
	struct _finddata_t found;
	intptr_t h;
	*count = 0;
	snprintf(pattern, sizeof pattern, "%s/*", dir);
	if ((h = _findfirst(pattern, &found)) == -1) {
		snprintf(err, err_size, "cannot list %s", dir);
		return NULL;
	}
	do {
		if (!(found.attrib & _A_SUBDIR) && matches(found.name, "", suffix) && add_name(&names, count, &cap, found.name) != 0)
			failed = 1;
	} while (!failed && _findnext(h, &found) == 0);
	_findclose(h);
#else
	DIR *d = opendir(dir);
	struct dirent *e;
	*count = 0;
	if (!d) {
		snprintf(err, err_size, "cannot list %s: %s", dir, strerror(errno));
		return NULL;
	}
	while (!failed && (e = readdir(d)) != NULL) {
		char path[KP_FS_PATH];
		struct stat st;
		if (e->d_name[0] == '.' || !matches(e->d_name, "", suffix)) continue;
		snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) continue; /* a folder is no file, as on Windows */
		if (add_name(&names, count, &cap, e->d_name) != 0) failed = 1;
	}
	closedir(d);
#endif
	if (!failed && !names && !(names = (char **)calloc(1, sizeof *names))) failed = 1;
	if (failed) {
		int k;
		for (k = 0; k < *count; k++) free(names[k]);
		free(names);
		snprintf(err, err_size, "out of memory");
		return NULL;
	}
	qsort(names, (size_t)*count, sizeof *names, cmp_names);
	return names;
}

int kp_fs_remove_matching(const char *dir, const char *prefix, const char *suffix, char *err, size_t err_size)
{
	char path[KP_FS_PATH];
	int removed = 0;
#ifdef _WIN32
	struct _finddata_t found;
	intptr_t h;
	snprintf(path, sizeof path, "%s/*", dir);
	if ((h = _findfirst(path, &found)) == -1) return 0;
	do {
		if (!(found.attrib & _A_SUBDIR) && matches(found.name, prefix, suffix)) {
			snprintf(path, sizeof path, "%s/%s", dir, found.name);
			if (remove(path) != 0) {
				snprintf(err, err_size, "cannot remove %s: %s", path, strerror(errno));
				_findclose(h);
				return -1;
			}
			removed++;
		}
	} while (_findnext(h, &found) == 0);
	_findclose(h);
#else
	DIR *d = opendir(dir);
	struct dirent *e;
	if (!d) return 0;
	while ((e = readdir(d)) != NULL) {
		if (!matches(e->d_name, prefix, suffix)) continue;
		snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		if (remove(path) != 0) {
			snprintf(err, err_size, "cannot remove %s: %s", path, strerror(errno));
			closedir(d);
			return -1;
		}
		removed++;
	}
	closedir(d);
#endif
	return removed;
}
