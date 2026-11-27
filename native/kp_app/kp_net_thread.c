/* A lock and threads for the network (kp_net_thread.h). */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L /* the threads under -std=c99 */
#endif
#include "kp_net_thread.h"

#include <stdlib.h>

#ifdef __EMSCRIPTEN__

KpNetLock *kp_net_lock_new(void) { return NULL; }
void kp_net_lock_free(KpNetLock *l) { (void)l; }
void kp_net_lock(KpNetLock *l) { (void)l; }
void kp_net_unlock(KpNetLock *l) { (void)l; }
KpNetThread *kp_net_thread_start(void (*fn)(void *ud), void *ud)
{
	(void)fn;
	(void)ud;
	return NULL;
}
void kp_net_thread_join(KpNetThread *t) { (void)t; }
int kp_net_thread_detach(void (*fn)(void *ud), void *ud)
{
	(void)fn;
	(void)ud;
	return 0;
}

#else

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

struct KpNetLock {
#ifdef _WIN32
	CRITICAL_SECTION cs;
#else
	pthread_mutex_t m;
#endif
};

struct KpNetThread {
	void (*fn)(void *ud);
	void *ud;
	int detached; /* the thread frees the record once fn returned */
#ifdef _WIN32
	HANDLE h;
#else
	pthread_t t;
#endif
};

KpNetLock *kp_net_lock_new(void)
{
	KpNetLock *l = (KpNetLock *)malloc(sizeof *l);
	if (!l) return NULL;
#ifdef _WIN32
	InitializeCriticalSection(&l->cs);
#else
	if (pthread_mutex_init(&l->m, NULL) != 0) {
		free(l);
		return NULL;
	}
#endif
	return l;
}

void kp_net_lock_free(KpNetLock *l)
{
	if (!l) return;
#ifdef _WIN32
	DeleteCriticalSection(&l->cs);
#else
	pthread_mutex_destroy(&l->m);
#endif
	free(l);
}

void kp_net_lock(KpNetLock *l)
{
	if (!l) return;
#ifdef _WIN32
	EnterCriticalSection(&l->cs);
#else
	pthread_mutex_lock(&l->m);
#endif
}

void kp_net_unlock(KpNetLock *l)
{
	if (!l) return;
#ifdef _WIN32
	LeaveCriticalSection(&l->cs);
#else
	pthread_mutex_unlock(&l->m);
#endif
}

/* The thread's body: fn, then a detached thread's record freed */
static void run(KpNetThread *t)
{
	t->fn(t->ud);
	if (t->detached) free(t);
}

#ifdef _WIN32
static DWORD WINAPI trampoline(LPVOID p)
{
	run((KpNetThread *)p);
	return 0;
}
#else
static void *trampoline(void *p)
{
	run((KpNetThread *)p);
	return NULL;
}
#endif

/* A thread's record, its thread not started */
static KpNetThread *record(void (*fn)(void *ud), void *ud, int detached)
{
	KpNetThread *t = fn ? (KpNetThread *)calloc(1, sizeof *t) : NULL;
	if (!t) return NULL;
	t->fn = fn;
	t->ud = ud;
	t->detached = detached;
	return t;
}

/* t's thread started: 1, else 0 (t is still the caller's). The handle goes to a local first: a detached thread may
 * free its record before the start returns. */
static int start(KpNetThread *t)
{
	int started, detached = t->detached;
#ifdef _WIN32
	HANDLE h = CreateThread(NULL, 0, trampoline, t, 0, NULL);
	started = h != NULL;
	if (started && detached) CloseHandle(h);
	else if (started) t->h = h; /* the join waits for the thread, not for this store */
#else
	pthread_attr_t attr;
	pthread_t id;
	started = pthread_attr_init(&attr) == 0;
	if (started) {
		started = (!detached || pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) == 0)
			&& pthread_create(&id, &attr, trampoline, t) == 0;
		pthread_attr_destroy(&attr);
	}
	if (started && !detached) t->t = id;
#endif
	return started;
}

KpNetThread *kp_net_thread_start(void (*fn)(void *ud), void *ud)
{
	KpNetThread *t = record(fn, ud, 0);
	if (t && !start(t)) {
		free(t);
		t = NULL;
	}
	return t;
}

void kp_net_thread_join(KpNetThread *t)
{
	if (!t) return;
#ifdef _WIN32
	WaitForSingleObject(t->h, INFINITE);
	CloseHandle(t->h);
#else
	pthread_join(t->t, NULL);
#endif
	free(t);
}

int kp_net_thread_detach(void (*fn)(void *ud), void *ud)
{
	KpNetThread *t = record(fn, ud, 1);
	if (t && !start(t)) {
		free(t);
		return 0;
	}
	return t != NULL; /* t is the thread's now */
}

#endif
