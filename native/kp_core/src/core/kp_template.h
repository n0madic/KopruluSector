/*
 * The game templates and the victory trigger files (gametype.cpp, atlas P15 0x447740..0x448530; ADR 0024): the list
 * Templates\templates.lst names, each template's .got file (its name, label and 32-byte record, KP_TEMPLATE_SIZE in
 * kp_start.h), and the triggers\*.trg files SetupVictoryConditions 0x448530 loads into a game's triggers. The data
 * reads them once (kp_app's KpData); the lobby and the start look them up.
 */
#ifndef KP_TEMPLATE_H
#define KP_TEMPLATE_H

#include <stddef.h>
#include <stdint.h>

#include "kp_files.h"
#include "kp_start.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_TEMPLATE_LIST "templates\\templates.lst"
#define KP_TEMPLATE_DIR "templates\\"
#define KP_TEMPLATE_EXT ".got"
#define KP_TEMPLATE_FILE_SIZE 97  /* ReadTemplateFile 0x4482a0: version byte, name[32], label[32], record[32] */
#define KP_TEMPLATE_FILE_VERSION 3
#define KP_TEMPLATE_ENTRY 260     /* NextQuotedString 0x447950's buffer: an entry of the list, its NUL included */
#define KP_TEMPLATE_TEXT 32       /* a template's name and label, NUL-terminated within */
#define KP_TEMPLATES 64           /* the retail list has 30 */

/* The template record's other fields (P15: +1 the type's sub-byte, +0xb fog, +0x11 cheats, +0x13 the victory value;
 * the type, subtype, display value, victory, resources, starting units, fixed positions, restriction, allies, teams,
 * tournament and resource value are kp_start.h's). */
#define KP_TEMPLATE_SUB 0x01
#define KP_TEMPLATE_VICTORY_VALUE 0x13
#define KP_TEMPLATE_TYPE_LIMIT 0x81 /* ReadTemplateFile refuses a type at or above it */
#define KP_TEMPLATE_SUBTYPE_LIMIT 8 /* and a subtype WORD at or above this */

typedef struct {
	char name[KP_TEMPLATE_TEXT];  /* "Melee" */
	char label[KP_TEMPLATE_TEXT]; /* the subtype's label ("Resources", "Number of Teams"; "" none) */
	uint8_t record[KP_TEMPLATE_SIZE];
} KpTemplate;

/* The template list in the file's order (AddTemplate 0x447850 links each at the tail). */
typedef struct KpTemplates {
	int count;
	KpTemplate list[KP_TEMPLATES];
} KpTemplates;

/* A .got file as ReadTemplateFile 0x4482a0 takes it: 97 bytes of version 3, a type below 0x81 and a subtype below 8.
 * 1 with *t filled, 0 refused. */
int kp_template_file(const uint8_t *data, size_t size, KpTemplate *t);
/* The next quoted entry of templates.lst from *at (NextQuotedString 0x447950): the characters between the next pair of
 * '"' into out (at most size - 1); 1 with *at past the closing quote, 0 when no pair is left or the entry does not fit. */
int kp_template_next_entry(const uint8_t *list, size_t size, size_t *at, char *out, size_t out_size);
/* ForEachTemplateFile 0x447740: each entry of templates.lst cut at its last '.', its .got read from Templates\ (a
 * missing or refused file skipped). 0, or -1 with err when the list cannot be read (the original's fatal "Unable to
 * read game templates."). */
int kp_templates_from_files(KpTemplates *t, const KpFiles *f, KpError *err);
/* FindTemplateRecord 0x447b50: the first template of type, sub-byte and subtype (NULL none). */
const KpTemplate *kp_templates_find(const KpTemplates *t, int type, int sub, int subtype);

/* The victory trigger files SetupVictoryConditions 0x448530 picks by the template (P15's table). */
#define KP_VICTORY_FILES 11
extern const char *const KP_VICTORY_FILE_NAMES[KP_VICTORY_FILES];
#define KP_VICTORY_RECORD 2400 /* 0x447f70 adds size / 2400 trigger records of a file */

typedef struct KpVictoryFiles {
	uint8_t *data[KP_VICTORY_FILES]; /* owned; NULL: the data has no such file (0x447f70 returns 0) */
	size_t size[KP_VICTORY_FILES];
} KpVictoryFiles;

/* The victory files of the data: 0, or -1 with err when a source fails to read one. */
int kp_victory_from_files(KpVictoryFiles *v, const KpFiles *f, KpError *err);
void kp_victory_free(KpVictoryFiles *v);

#ifdef __cplusplus
}
#endif

#endif
