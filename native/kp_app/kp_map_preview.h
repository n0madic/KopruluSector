/*
 * The chat room's preview world of a map file (InitGameWorldForLobbyPreview 0x443850's loads: the scenario's melee
 * sections, its tileset, the DAT's units) as the lobby's minimap takes it (KpUiPreview; kp_preview.h builds the world).
 * The product's flow and the screens' oracle answer KpNetUi.preview with it.
 */
#ifndef KP_MAP_PREVIEW_H
#define KP_MAP_PREVIEW_H

#include <stdint.h>

#include "kp_dat.h"
#include "kp_files.h"
#include "kp_preview.h"
#include "kp_present.h"
#include "kp_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	KpPreview world;
	KpTileset tileset;
	int loaded; /* world and tileset are owned */
} KpMapPreview;

/* The preview world of the map at path (an MPQ the data's files read through; templates: the lobby's record selects
 * the melee filter; expansion: the expansion-only types) into *out, valid until kp_map_preview_free: 0, or -1 with
 * the reason in err (nothing kept). A world loaded before is freed first. */
int kp_map_preview_load(KpMapPreview *mp, const KpFiles *files, const KpDat *dat, const char *path,
	const uint8_t *template_record, int expansion, KpUiPreview *out, KpError *err);
void kp_map_preview_free(KpMapPreview *mp);

#ifdef __cplusplus
}
#endif

#endif
