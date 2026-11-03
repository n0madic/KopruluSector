/* The helpers of the C suites that run the product's own code (native/kp_app: test_session.c, test_flow.c). */
#ifndef KP_TEST_APP_H
#define KP_TEST_APP_H

#include "kp_host.h"
#include "kp_test.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The data every game shares (generated/data/grpmeta.kpd, iscript.kpd, aiscript.kpd), parsed once in the suite's
 * process as the product does; fails without them. */
const KpHostShared *kp_t_shared(KpT *T);

#ifdef __cplusplus
}
#endif

#endif
