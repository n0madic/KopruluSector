/* The chat room's preview world of a map file (kp_map_preview.h). */
#include "kp_map_preview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_chk.h"
#include "kp_data.h"
#include "kp_derive.h"

int kp_map_preview_load(KpMapPreview *mp, const KpFiles *files, const KpDat *dat, const char *path,
	const uint8_t *template_record, int expansion, KpUiPreview *out, KpError *err)
{
	KpDataMap map;
	KpChk chk;
	uint8_t *bytes;
	size_t size;
	char why[KP_ERROR_SIZE];
	int tileset, rc;
	kp_map_preview_free(mp);
	if (kp_data_map_open(&map, files, path, err) != 0) return -1;
	bytes = kp_data_map_chk(&map, &size, err);
	kp_data_map_close(&map);
	if (bytes == NULL) return -1;
	if (kp_chk_load(&chk, bytes, size, files->profile, expansion, KP_CHK_MELEE, why, sizeof why) != 0) {
		snprintf(err->msg, sizeof err->msg, "preview: %s", why);
		free(bytes);
		return -1;
	}
	if ((tileset = kp_preview_tileset(&chk)) < 0) {
		snprintf(err->msg, sizeof err->msg, "preview: no DIM or ERA");
		rc = -1;
	} else if (kp_tileset_from_files(&mp->tileset, files, tileset, err) != 0) {
		rc = -1;
	} else if (kp_preview_build(&mp->world, &chk, mp->tileset.cv5, (size_t)mp->tileset.groups * KP_DERIVE_CV5_GROUP, dat,
		template_record, expansion, why, sizeof why) != 0) {
		snprintf(err->msg, sizeof err->msg, "%s", why);
		kp_tileset_free(&mp->tileset);
		rc = -1;
	} else {
		rc = 0;
	}
	kp_chk_free(&chk);
	free(bytes);
	if (rc != 0) return -1;
	mp->loaded = 1;
	memset(out, 0, sizeof *out);
	out->map_w = (int32_t)mp->world.width;
	out->map_h = (int32_t)mp->world.height;
	out->megatiles = mp->world.megatiles;
	out->mmni = mp->tileset.mmni;
	out->mmni_size = mp->tileset.mmni_size;
	out->rgb = mp->tileset.rgb;
	out->cycled = mp->tileset.cycled;
	out->units = mp->world.units;
	out->unit_count = mp->world.unit_count;
	return 0;
}

void kp_map_preview_free(KpMapPreview *mp)
{
	if (mp->loaded) {
		kp_preview_free(&mp->world);
		kp_tileset_free(&mp->tileset);
	}
	memset(mp, 0, sizeof *mp);
}
