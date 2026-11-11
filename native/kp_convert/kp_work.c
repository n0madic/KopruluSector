#ifndef _WIN32
#define _DEFAULT_SOURCE /* glibc's sysconf(_SC_NPROCESSORS_ONLN) under -std=c99; macOS shows it unless a POSIX level is asked */
#endif
#include "kp_work.h"

#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#define KP_WORK_THREADS_MAX 64

typedef struct {
	KpWorkFn fn;
	void *ctx;
	int jobs, next;
#ifdef _WIN32
	CRITICAL_SECTION lock;
#else
	pthread_mutex_t lock;
#endif
} Work;

static int take(Work *w)
{
	int k;
#ifdef _WIN32
	EnterCriticalSection(&w->lock);
#else
	pthread_mutex_lock(&w->lock);
#endif
	k = w->next < w->jobs ? w->next++ : -1;
#ifdef _WIN32
	LeaveCriticalSection(&w->lock);
#else
	pthread_mutex_unlock(&w->lock);
#endif
	return k;
}

static void drain(Work *w)
{
	int k;
	while ((k = take(w)) >= 0) w->fn(w->ctx, k);
}

#ifdef _WIN32
static DWORD WINAPI worker(LPVOID p)
{
	drain((Work *)p);
	return 0;
}
#else
static void *worker(void *p)
{
	drain((Work *)p);
	return NULL;
}
#endif

void kp_work_run(KpWorkFn fn, void *ctx, int jobs, int threads)
{
	Work w;
	int k, started = 0;
#ifdef _WIN32
	HANDLE t[KP_WORK_THREADS_MAX];
#else
	pthread_t t[KP_WORK_THREADS_MAX];
#endif
	if (jobs <= 0) return;
	w.fn = fn;
	w.ctx = ctx;
	w.jobs = jobs;
	w.next = 0;
	if (threads > jobs) threads = jobs;
	if (threads > KP_WORK_THREADS_MAX) threads = KP_WORK_THREADS_MAX;
	if (threads <= 1) {
		for (k = 0; k < jobs; k++) fn(ctx, k);
		return;
	}
#ifdef _WIN32
	InitializeCriticalSection(&w.lock);
	for (k = 1; k < threads; k++)
		if ((t[started] = CreateThread(NULL, 0, worker, &w, 0, NULL)) != NULL) started++;
#else
	pthread_mutex_init(&w.lock, NULL);
	for (k = 1; k < threads; k++)
		if (pthread_create(&t[started], NULL, worker, &w) == 0) started++;
#endif
	drain(&w); /* the calling thread works too; a thread that could not start leaves its share to the others */
#ifdef _WIN32
	for (k = 0; k < started; k++) {
		WaitForSingleObject(t[k], INFINITE);
		CloseHandle(t[k]);
	}
	DeleteCriticalSection(&w.lock);
#else
	for (k = 0; k < started; k++) pthread_join(t[k], NULL);
	pthread_mutex_destroy(&w.lock);
#endif
}

int kp_work_processors(void)
{
#ifdef _WIN32
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	return info.dwNumberOfProcessors > 0 ? (int)info.dwNumberOfProcessors : 1;
#else
	long n = sysconf(_SC_NPROCESSORS_ONLN);
	return n > 0 ? (int)n : 1;
#endif
}

double kp_work_seconds(void)
{
#ifdef _WIN32
	LARGE_INTEGER t, f;
	QueryPerformanceCounter(&t);
	QueryPerformanceFrequency(&f);
	return (double)t.QuadPart / (double)f.QuadPart;
#else
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
#endif
}
