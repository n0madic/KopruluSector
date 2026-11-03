/* The product's helpers of the C suites (kp_test_app.h). */
#include "kp_test_app.h"

#define DATA "generated/data/"

const KpHostShared *kp_t_shared(KpT *T)
{
	static KpHostShared sh;
	static int loaded;
	size_t grp_size, iscript_size, aiscript_size;
	const uint8_t *grp, *iscript, *aiscript;
	KpError err;
	if (loaded) return &sh;
	grp = kp_t_read(T, DATA "grpmeta.kpd", &grp_size);
	iscript = kp_t_read(T, DATA "iscript.kpd", &iscript_size);
	aiscript = kp_t_read(T, DATA "aiscript.kpd", &aiscript_size);
	if (kp_host_shared_load(&sh, grp, grp_size, iscript, iscript_size, aiscript, aiscript_size, &err) != 0) {
		kp_t_fail(T, "%s", err.msg);
	}
	loaded = 1;
	return &sh;
}
