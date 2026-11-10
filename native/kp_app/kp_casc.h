/* StarCraft: Remastered's CASC storage through CascLib (ADR 0018), for C: CascLib.h includes C++ headers, so its calls
 * live in kp_casc.cpp. A storage handle is not to be shared between threads: each thread opens its own. */
#ifndef KP_CASC_H
#define KP_CASC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KpCasc KpCasc;

/* The local storage of the install at root (the folder that holds .build.info), or NULL with the reason in err. */
KpCasc *kp_casc_open(const char *root, char *err, size_t err_size);
void kp_casc_close(KpCasc *c);
/* Whether the storage names a file ('/' separated, any case: "SD/arr/units.dat"). */
int kp_casc_has(KpCasc *c, const char *name);
/* A malloc'd copy of a file's bytes (NUL terminated past size), or NULL: err empty when the storage does not name
 * it, the reason otherwise (named but not installed locally, unreadable). */
uint8_t *kp_casc_read(KpCasc *c, const char *name, size_t *size, char *err, size_t err_size);

#ifdef __cplusplus
}
#endif

#endif
