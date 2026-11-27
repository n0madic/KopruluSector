/*
 * A lock and threads for the network (phase N): Storm's pump on a thread of its own (kp_netplay.h) and a host name's
 * lookup (kp_net_sock.h). POSIX threads or Windows'; the web has none: kp_net_lock_new and the starts answer NULL and 0
 * there, and their callers work on the calling thread.
 */
#ifndef KP_NET_THREAD_H
#define KP_NET_THREAD_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KpNetLock KpNetLock;

/* A lock (not recursive); NULL out of resources or on the web. */
KpNetLock *kp_net_lock_new(void);
void kp_net_lock_free(KpNetLock *l);
/* NULL: nothing. */
void kp_net_lock(KpNetLock *l);
void kp_net_unlock(KpNetLock *l);

typedef struct KpNetThread KpNetThread;

/* fn(ud) on a thread of its own, joined by kp_net_thread_join; NULL when none started. */
KpNetThread *kp_net_thread_start(void (*fn)(void *ud), void *ud);
/* Waits until the thread's fn returned and frees it (NULL nothing). */
void kp_net_thread_join(KpNetThread *t);
/* fn(ud) on a detached thread, which nobody joins: 1 started. */
int kp_net_thread_detach(void (*fn)(void *ud), void *ud);

#ifdef __cplusplus
}
#endif

#endif
