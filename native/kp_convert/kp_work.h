/*
 * kp_convert's workers (ADR 0019): jobs run on a few threads, each by its index, the call returning when every job has
 * run. POSIX threads, or Windows' own; and the clock the commands time themselves with.
 */
#ifndef KP_WORK_H
#define KP_WORK_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*KpWorkFn)(void *ctx, int job);

/* fn(ctx, k) for every k in [0, jobs), on up to threads threads (1 or fewer: on the calling thread, in order). */
void kp_work_run(KpWorkFn fn, void *ctx, int jobs, int threads);
/* The processors online (at least 1). */
int kp_work_processors(void);
/* A monotonic clock's seconds (the commands' timings). */
double kp_work_seconds(void);

#ifdef __cplusplus
}
#endif

#endif
