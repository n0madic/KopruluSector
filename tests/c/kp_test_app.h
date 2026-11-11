/* The helpers of the C suites that run the product's own code (native/kp_app: test_session.c, test_flow.c). */
#ifndef KP_TEST_APP_H
#define KP_TEST_APP_H

#include "kp_host.h"
#include "kp_res.h"
#include "kp_test.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The install the environment variable env names (KP_TEST_ORIGINAL: 1.17, KP_TEST_SCR: StarCraft: Remastered) opened
 * through kp_res, closed when the test ends; skips the test when env is not set, fails when the install does not
 * open. */
KpRes *kp_t_install(KpT *T, const char *env);

#ifdef __cplusplus
}
#endif

#endif
