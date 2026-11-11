/* The product's helpers of the C suites (kp_test_app.h). */
#include "kp_test_app.h"

#include <stdlib.h>
#include <string.h>

/* kp_res_close as a deferred cleanup (a call through another function type is undefined) */
static void res_close(void *p) { kp_res_close((KpRes *)p); }

KpRes *kp_t_install(KpT *T, const char *env)
{
	const char *dir = getenv(env), *dirs[1];
	KpResConfig c;
	char err[1024];
	KpRes *r;
	if (!dir || !dir[0]) kp_t_skip(T, "no %s", env);
	memset(&c, 0, sizeof c);
	dirs[0] = dir;
	r = kp_res_open(dirs, 1, &c, err, sizeof err);
	kp_t_ok(T, r != NULL, "%s: %s", dir, err);
	kp_t_defer(T, res_close, r);
	return r;
}
