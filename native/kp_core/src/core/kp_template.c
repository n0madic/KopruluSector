/* The game templates and the victory trigger files (kp_template.h). */
#include "kp_template.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SetupVictoryConditions 0x448530's files: Melee (types 1, 7), Sudden Death (5), Greed by its value (3), Slaughter by
 * its minutes (6), Capture the Flag (4, after the flag beacons). */
const char *const KP_VICTORY_FILE_NAMES[KP_VICTORY_FILES] = {
	"triggers\\Melee.trg",
	"triggers\\SuddenDeath.trg",
	"triggers\\Greed2500.trg",
	"triggers\\Greed5000.trg",
	"triggers\\Greed7500.trg",
	"triggers\\Greed10000.trg",
	"triggers\\Slaughter15.trg",
	"triggers\\Slaughter30.trg",
	"triggers\\Slaughter45.trg",
	"triggers\\Slaughter60.trg",
	"triggers\\CTF.trg",
};

static void copy_text(char *out, const uint8_t *in)
{
	memcpy(out, in, KP_TEMPLATE_TEXT);
	out[KP_TEMPLATE_TEXT - 1] = 0;
}

int kp_template_file(const uint8_t *data, size_t size, KpTemplate *t)
{
	const uint8_t *rec;
	if (!data || size != KP_TEMPLATE_FILE_SIZE || data[0] != KP_TEMPLATE_FILE_VERSION) return 0;
	rec = data + 1 + 2 * KP_TEMPLATE_TEXT;
	if (rec[KP_TEMPLATE_TYPE] >= KP_TEMPLATE_TYPE_LIMIT || (rec[KP_TEMPLATE_SUBTYPE] | rec[KP_TEMPLATE_SUBTYPE + 1] << 8) >= KP_TEMPLATE_SUBTYPE_LIMIT)
		return 0;
	memset(t, 0, sizeof *t);
	copy_text(t->name, data + 1);
	copy_text(t->label, data + 1 + KP_TEMPLATE_TEXT);
	memcpy(t->record, rec, KP_TEMPLATE_SIZE);
	return 1;
}

int kp_template_next_entry(const uint8_t *list, size_t size, size_t *at, char *out, size_t out_size)
{
	size_t k = *at, n = 0;
	while (k < size && list[k] != '"') k++;
	if (k >= size) return 0;
	for (k++; k < size && list[k] != '"'; k++) {
		if (n + 1 >= out_size) return 0; /* no room for the terminator */
		out[n++] = (char)list[k];
	}
	if (k >= size) return 0; /* no closing quote */
	out[n] = 0;
	*at = k + 1;
	return 1;
}

int kp_templates_from_files(KpTemplates *t, const KpFiles *f, KpError *err)
{
	size_t size, at = 0;
	uint8_t *list;
	char entry[KP_TEMPLATE_ENTRY];
	memset(t, 0, sizeof *t);
	if (!(list = kp_files_need(f, KP_TEMPLATE_LIST, &size, err))) return -1;
	while (t->count < KP_TEMPLATES && kp_template_next_entry(list, size, &at, entry, sizeof entry)) {
		char path[sizeof KP_TEMPLATE_DIR + KP_TEMPLATE_ENTRY + sizeof KP_TEMPLATE_EXT], why[KP_ERROR_SIZE];
		char *dot = strrchr(entry, '.');
		uint8_t *got;
		size_t got_size;
		if (dot) *dot = 0; /* the entry names its .got file: cut at its last '.' */
		snprintf(path, sizeof path, "%s%s%s", KP_TEMPLATE_DIR, entry, KP_TEMPLATE_EXT);
		why[0] = 0;
		got = f->read(f->ctx, path, &got_size, why, sizeof why);
		if (!got && why[0]) {
			snprintf(err->msg, sizeof err->msg, "templates: %s: %s", path, why);
			free(list);
			return -1;
		}
		if (kp_template_file(got, got ? got_size : 0, &t->list[t->count])) t->count++;
		free(got);
	}
	free(list);
	return 0;
}

const KpTemplate *kp_templates_find(const KpTemplates *t, int type, int sub, int subtype)
{
	int k;
	for (k = 0; k < t->count; k++) {
		const uint8_t *r = t->list[k].record;
		if (r[KP_TEMPLATE_TYPE] == type && r[KP_TEMPLATE_SUB] == sub && (r[KP_TEMPLATE_SUBTYPE] | r[KP_TEMPLATE_SUBTYPE + 1] << 8) == subtype)
			return &t->list[k];
	}
	return NULL;
}

int kp_victory_from_files(KpVictoryFiles *v, const KpFiles *f, KpError *err)
{
	int k;
	memset(v, 0, sizeof *v);
	for (k = 0; k < KP_VICTORY_FILES; k++) {
		char why[KP_ERROR_SIZE];
		why[0] = 0;
		v->data[k] = f->read(f->ctx, KP_VICTORY_FILE_NAMES[k], &v->size[k], why, sizeof why);
		if (!v->data[k] && why[0]) {
			snprintf(err->msg, sizeof err->msg, "victory: %s: %s", KP_VICTORY_FILE_NAMES[k], why);
			kp_victory_free(v);
			return -1;
		}
		if (!v->data[k]) v->size[k] = 0;
	}
	return 0;
}

void kp_victory_free(KpVictoryFiles *v)
{
	int k;
	for (k = 0; k < KP_VICTORY_FILES; k++) free(v->data[k]);
	memset(v, 0, sizeof *v);
}
