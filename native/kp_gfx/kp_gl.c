/* The GL function table of Windows and Linux (kp_gl.h). */
#include "kp_gl.h"

#ifdef KP_GL_TABLE
#define KP_GL_DEFINE(ret, name, params) kp_pfn_##name kp_##name;
KP_GL_FUNCTIONS(KP_GL_DEFINE)
#undef KP_GL_DEFINE

const char *kp_gl_load(void)
{
#define KP_GL_LOAD(ret, name, params) \
	if (!(kp_##name = (kp_pfn_##name)SDL_GL_GetProcAddress(#name))) return #name;
	KP_GL_FUNCTIONS(KP_GL_LOAD)
#undef KP_GL_LOAD
	return NULL;
}
#else
const char *kp_gl_load(void) { return NULL; }
#endif
