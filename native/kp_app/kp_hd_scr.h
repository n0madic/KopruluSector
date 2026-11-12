/*
 * The HD world from a StarCraft: Remastered install (ADR 0019, stage C5): the images' anims, the HD tilesets and the
 * water's normal maps read from their own files (kp_hd_scr.c), for the runtime when a mission starts and for
 * kp_convert repack, which writes the containers into the repack's HD groups (KP_HD_REPACK_*).
 */
#ifndef KP_HD_SCR_H
#define KP_HD_SCR_H

#include <stddef.h>
#include <stdint.h>

#include "kp_grpmeta.h"
#include "kp_present.h"
#include "kp_res.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The variants: HD2 (x2) and HD (x4) */
enum { KP_HD_SCR_HD2, KP_HD_SCR_HD, KP_HD_SCR_VARIANTS };
/* Which images draw another's frames (u32, i32 a record: the image, -1 its own) */
#define KP_HD_SCR_RELATIONS "SD/images.rel"

/* The repack's HD containers by their names (kp_convert repack; ADR 0019, stage C5), a set's: a variant's pages as BC
 * (the set "hd2", "hd") or transcoded for phones ("hd2-astc", "hd2-etc2": kp_hd_scr_set_suffix). In the set's group
 * (KP_HD_REPACK_GROUP, read by ranges, stored uncompressed) each SD pack's HDPK, each tileset's HDTS and the water's
 * HDWA; in the core each pack's head (its first bytes up to its frames' end: kp_hd_pack_head_size), which the
 * runtime opens the pack from, on the web too, before it reads any page. */
#define KP_HD_REPACK_GROUP "koprulu-%s.mpq"         /* the set */
#define KP_HD_REPACK_PREFIX "koprulu\\%s\\"         /* its names */
#define KP_HD_REPACK_PACK "koprulu\\%s\\packs\\%s.kpd"
#define KP_HD_REPACK_TILESET "koprulu\\%s\\tilesets\\%s.kpd"
#define KP_HD_REPACK_WATER "koprulu\\%s\\water.kpd"
#define KP_HD_REPACK_HEAD "koprulu\\heads\\%s\\%s.kpd" /* the set, the pack */

const char *kp_hd_scr_variant_name(int variant);
int32_t kp_hd_scr_variant_scale(int variant);
/* Its files' prefix in the install ("HD2/", ""). */
const char *kp_hd_scr_variant_prefix(int variant);
/* A variant by its name ("hd2", "hd"), -1 none. */
int kp_hd_scr_variant_of(const char *name);
/* The suffix of a variant's k-th set's name in a repack, in the order the runtime prefers them where the context takes
 * their blocks ("" BC, "-astc", "-etc2"), and its pages' format (KP_HD_FORMAT_*; NULL: not wanted); NULL past the
 * last (KP_HD_SCR_SETS of them). */
#define KP_HD_SCR_SETS 3
const char *kp_hd_scr_set_suffix(int k, int *format);
/* A set's variant and pages' format by its name ("hd2-astc"): 0, or -1 when it names none. */
int kp_hd_scr_set(const char *set, int *variant, int *format);
/* An image's anim by its name in the install ("HD2/anim/main_042.anim"): 0, -1 when out is too small. */
int kp_hd_scr_anim_name(int variant, int32_t image, char *out, size_t size);
/* Whether a Remastered install holds the variant's anims (its first, and images.rel). */
int kp_hd_scr_held(KpRes *r, int variant);
/* Which image's anim draws each GRP entry of grpmeta g: per entry 1..grp_count, malloc'd, the first
 * image of images.dat naming its GRP whose own anim the variant has, else whose images.rel's image's anim it has; -1
 * none. 0, or -1 with err. */
int kp_hd_scr_owners(KpRes *r, const KpGrpMeta *g, const KpDat *dat, int variant, int32_t **owner_of, KpError *err);
/* SD pack `pack`'s HD layout (kp_hd_layout_build at KP_HD_PAGE_SIZE): the anims of the images drawing its entries read
 * through h into anims (by image, image_count of them, the caller's: those read for an earlier pack are kept). 0, or -1
 * with err. */
int kp_hd_scr_layout(KpRes *r, KpResReader *h, const KpGrpMeta *g, const int32_t *owner_of, int variant, const char *pack,
	KpAnim *anims, int32_t image_count, KpHdLayout *out, KpError *err);
/* The anims' sheets a layout's pages are built from (kp_hd_layout_page's KpHdAnimRead), read through a reader on any
 * thread: the install's folder (a place of the install's: kp_res_raw_place) and the variant. */
typedef struct {
	KpResReader *reader;
	KpResPlace install;
	int variant;
} KpHdScrSheets;
int kp_hd_scr_sheets_read(void *ctx, int32_t image, uint64_t offset, void *out, size_t size);
/* Tileset's HD container (HDTS, KP_TILESETS' index): its 1.17 megatiles' BC1 tiles, the water's
 * masks where it has water, creep's edges. Malloc'd bytes, or NULL with err. */
uint8_t *kp_hd_scr_tileset(KpRes *r, int variant, int tileset, size_t *size, KpError *err);
/* The water's normal maps (HDWA). Malloc'd bytes, or NULL with err. */
uint8_t *kp_hd_scr_water(KpRes *r, int variant, size_t *size, KpError *err);

#ifdef __cplusplus
}
#endif

#endif
