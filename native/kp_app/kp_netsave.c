/* StarCraft 1.17's multiplayer saves as a network lobby reads them (kp_netsave.h). */
#include "kp_netsave.h"

#include <stdlib.h>
#include <string.h>

#include "kp_net.h"
#include "kp_rep.h"

#define BANNER_END 0x1a
#define TAG_HIGH 0x0069u        /* ReadSaveTagAndHeader 0x49a0c0: (tag & 0xffff0000) == 0x00690000 */
#define HEADER_RNG 4
#define HEADER_GAME 0xc
#define HEADER_NAME 0x99
#define NAME_TRUNCATED 31       /* ValidateSaveFile 0x499750: the name truncated to 31 characters */
#define PLAYER_RECORD 36
/* the core block's fields (their addresses less 0x5263d0) */
#define CORE_LOCAL_SLOT 0xc0    /* 0x526490 */
#define CORE_TYPES 0xc4         /* 0x526494 */
#define CORE_RACES 0xd0         /* 0x5264a0 */
#define CORE_WIDTH 0xe4         /* 0x5264b4 */
#define CORE_HEIGHT 0xe6        /* 0x5264b6 */
#define CORE_TILESET 0xec       /* 0x5264bc */
#define CORE_FORCES 0xe4c0      /* 0x534890 */
#define CORE_FORCE_NAMES 0xe4cc /* 0x53489c */
#define CORE_EXPANSION 0x10350  /* 0x536720 */

/* The banner up to its 0x1a, the tag and the header block (ReadSaveTagAndHeader 0x49a0c0): 1 with *pos after it */
static int read_header(const uint8_t *data, size_t size, size_t *pos, uint8_t header[KP_NETSAVE_HEADER])
{
	const uint8_t *end = (const uint8_t *)memchr(data, BANNER_END, size);
	size_t at;
	if (!end) return 0;
	at = (size_t)(end - data) + 1;
	if (size - at < 4 || kp_net_get32(data + at) >> 16 != TAG_HIGH) return 0;
	at += 4;
	if (kp_rep_block_read(data, size, &at, header, KP_NETSAVE_HEADER) != 0) return 0;
	*pos = at;
	return 1;
}

/* ValidateSaveFile's display name: 1 when it fits */
static int display_name(const char *file, char name[KP_NETSAVE_NAME])
{
	const char *base = file, *p, *dot;
	char cut[NAME_TRUNCATED + 1];
	size_t n;
	for (p = file; *p; p++)
		if (*p == '/' || *p == '\\') base = p + 1;
	for (n = 0; n < NAME_TRUNCATED && base[n]; n++) cut[n] = base[n];
	cut[n] = 0;
	if ((dot = strrchr(cut, '.')) != NULL) cut[dot - cut] = 0;
	if (strlen(cut) >= KP_NETSAVE_NAME) return 0;
	memcpy(name, cut, strlen(cut) + 1);
	return 1;
}

int kp_netsave_probe(const uint8_t *data, size_t size, const char *file, KpNetGameHeader *h, char *name)
{
	uint8_t header[KP_NETSAVE_HEADER];
	char shown[KP_NETSAVE_NAME];
	size_t pos;
	if (!read_header(data, size, &pos, header)) return KP_NETSAVE_UNREADABLE;
	if (kp_net_get16(header) != KP_NETSAVE_VERSION) return KP_NETSAVE_OTHER_VERSION;
	if (!display_name(file, shown)) return KP_NETSAVE_UNREADABLE;
	if (h) kp_netgame_header_read(header + HEADER_GAME, h);
	if (name) memcpy(name, shown, sizeof shown);
	return KP_NETSAVE_VALID;
}

/* A string of the core block: its bytes up to its NUL, the last one forced to 0 */
static void core_string(char *dst, const uint8_t *src, size_t size)
{
	memcpy(dst, src, size);
	dst[size - 1] = 0;
}

int kp_netsave_read(const uint8_t *data, size_t size, int expansion, KpNetGameSave *out)
{
	uint8_t header[KP_NETSAVE_HEADER], players[KP_NETSAVE_PLAYERS];
	uint8_t *core;
	size_t pos;
	int k, ok = 0;
	memset(out, 0, sizeof *out);
	if (!read_header(data, size, &pos, header)) return 0;
	if (kp_rep_block_read(data, size, &pos, players, sizeof players) != 0) return 0;
	if (!(core = (uint8_t *)malloc(KP_NETSAVE_CORE))) return 0;
	if (kp_rep_block_read(data, size, &pos, core, KP_NETSAVE_CORE) != 0 || size - pos < 4) goto done;
	/* the Brood War byte: a Brood War save needs Brood War, and it must be this game's */
	if (core[CORE_EXPANSION] != (expansion ? 1 : 0)) goto done;
	kp_netgame_header_read(header + HEADER_GAME, &out->header);
	out->rng_state = kp_net_get32(header + HEADER_RNG);
	for (k = 0; k < KP_NETGAME_SLOTS; k++) {
		const uint8_t *r = players + k * PLAYER_RECORD;
		KpNetSlot *s = &out->players[k];
		s->slot = (int32_t)kp_net_get32(r);
		s->storm = (int32_t)kp_net_get32(r + 4);
		s->type = r[8];
		s->race = r[9];
		s->force = r[0xa];
		core_string(s->name, r + 0xb, sizeof s->name);
	}
	out->local_slot = core[CORE_LOCAL_SLOT];
	memcpy(out->types, core + CORE_TYPES, sizeof out->types);
	memcpy(out->races, core + CORE_RACES, sizeof out->races);
	memcpy(out->forces, core + CORE_FORCES, sizeof out->forces);
	for (k = 0; k < KP_NETGAME_FORCES; k++)
		core_string(out->force_names[k], core + CORE_FORCE_NAMES + k * KP_NETGAME_FORCE_NAME, KP_NETGAME_FORCE_NAME);
	out->width = (uint16_t)kp_net_get16(core + CORE_WIDTH);
	out->height = (uint16_t)kp_net_get16(core + CORE_HEIGHT);
	out->tileset = (uint16_t)kp_net_get16(core + CORE_TILESET);
	out->local_player = kp_net_get32(data + pos);
	ok = 1;
done:
	free(core);
	return ok;
}
