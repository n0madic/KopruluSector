/* StarCraft: Remastered's own formats brought back to 1.17's files (ADR 0018, stage D6): the C of
 * converter/casc/rebuild.py, which kp_res's reads of a Remastered install use where Remastered has no copy of a file.
 * Remastered keeps the glue and console art as RGBA images (DDS: uncompressed, DXT1 or DXT5) where 1.17 has
 * palette-indexed PCX and GRP files, its string tables as XML and no dialog BINs; the profile's compiled tables (SPAL,
 * SPCX, SGRP, SSMK, STBL, SBIN; tools/defs_c.py) say how each file is made again. */
#ifndef KP_SCR_H
#define KP_SCR_H

#include <stddef.h>
#include <stdint.h>

#include "kp_index_map.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_SCR_ERR 256
#define KP_SCR_TBL_NUL_IN_XML 0x07 /* Remastered's XML writes a NUL inside a string so */

/* An RGBA image, rows top down */
typedef struct {
	uint32_t width, height;
	uint8_t *rgba;
} KpScrImage;

/* A DDS file (dds.decode): uncompressed 32-bit R, G, B, A in byte order, DXT1 or DXT5; 0, -1 with err. */
int kp_scr_dds(const uint8_t *data, size_t size, KpScrImage *out, char *err, size_t err_size);
void kp_scr_image_free(KpScrImage *im);

/* A *.dds.grp's images (dds.container): u32 file size, u16 count, u16 version, then per image u32 0, u16 width, u16
 * height, u32 size and the DDS file. */
typedef struct {
	uint32_t width, height;
	const uint8_t *dds; /* into the container */
	size_t size;
} KpScrDdsEntry;
/* The entries, malloc'd (*count of them); -1 for a malformed container. */
int kp_scr_dds_container(const uint8_t *data, size_t size, KpScrDdsEntry **entries, uint32_t *count);

/* Whether rebuild.fit brings a w x h image to width x height. */
int kp_scr_fits(uint32_t w, uint32_t h, uint32_t width, uint32_t height);
/* rebuild.fit in place: the image brought to width columns and at most height rows (a block-compressed image's padding
 * cut, an HD image of a whole multiple of the size sampled at each block's top left, a shorter image kept as the bottom
 * rows); *above the rows above it. -1 when it does not fit. */
int kp_scr_fit(KpScrImage *im, uint32_t width, uint32_t height, uint32_t *above);
/* The image's palette indices (rebuild.indices) */
void kp_scr_indices(const KpScrImage *im, KpIndexMap *map, uint8_t *out);

/* A PCX as 1.17's files are written (rebuild.pcx_file): the standard RLE, header bytes 12-15 and 68-69 from header[6],
 * the palette after its marker; malloc'd, NULL out of memory. */
uint8_t *kp_scr_pcx(uint32_t width, uint32_t height, const uint8_t *pixels, const uint8_t palette[768],
	const uint8_t header[6], size_t *size);
/* The palette of a PCX (its last 768 bytes after the 0x0c marker): 0, -1 when it has none. */
int kp_scr_pcx_palette(const uint8_t *data, size_t size, uint8_t palette[768]);

/* A frame record of a GRP of plain rows (the glue's Dlg.grp) */
typedef struct {
	uint8_t x, y, w, h;
	uint32_t offset;
} KpScrGrpRecord;
/* rebuild.uncompressed_grp: `size` bytes, the header (count, box), the records, frame k's w * h pixels at its offset
 * (frames sharing rows keep the first's); NULL when a frame lies outside size or out of memory. */
uint8_t *kp_scr_plain_grp(uint32_t box_w, uint32_t box_h, const KpScrGrpRecord *records, uint32_t count,
	uint8_t *const *frames, size_t size);
/* rebuild.remap_grp: a row-compressed GRP drawn in another palette, each opaque pixel the index of its colour in the
 * WPE (256 x R, G, B, flag) by map; re-encoded as grp.encode; malloc'd, NULL for a malformed GRP. */
uint8_t *kp_scr_remap_grp(const uint8_t *grp, size_t size, const uint8_t *wpe, size_t wpe_size, KpIndexMap *map,
	size_t *out_size);

/* How kp_scr_xml_strings gives a value */
#define KP_SCR_XML_1252 0     /* in windows-1252 (rebuild.xml_strings) */
#define KP_SCR_XML_RC3 1      /* in windows-1252, RC3.xml's escapes undone (rebuild.unescape_rc3) */
#define KP_SCR_XML_LANGUAGE 2 /* a language's: its UTF-8 as the file has it, the escapes undone (rebuild.unescape_utf8) */
/* A string of Remastered's XML: its id and its value (KP_SCR_XML_*); bad: a language's escape is past a byte, and the
 * value is the file's with its entities undone */
typedef struct {
	char *id;
	uint8_t *value;
	size_t size;
	int bad;
} KpScrString;
typedef struct {
	KpScrString *items;
	uint32_t count;
} KpScrStrings;
/* 0, -1 with err (out of memory; but for a language's, a value not UTF-8 or an escape past a byte). */
int kp_scr_xml_strings(const uint8_t *xml, size_t size, int mode, KpScrStrings *out, char *err, size_t err_size);
void kp_scr_strings_free(KpScrStrings *s);
/* The value of an id, the last of equal ids (a dict's); NULL when none. */
const KpScrString *kp_scr_string(const KpScrStrings *s, const char *id, size_t id_len);

/* A TBL's strings (rebuild.tbl_spans): each up to the next one, its final NUL removed; -1 when not laid out in order.
 * spans[k] points into data. */
typedef struct {
	const uint8_t *data;
	size_t size;
} KpScrSpan;
int kp_scr_tbl_spans(const uint8_t *data, size_t size, KpScrSpan **spans, uint32_t *count);
/* rebuild.tbl_encode: malloc'd, NULL when the offsets outgrow 16 bits or out of memory. */
uint8_t *kp_scr_tbl(const KpScrSpan *spans, uint32_t count, size_t *size);

/* A dialog's text put back (rebuild.dialog_rebuild) */
typedef struct {
	uint32_t offset, length;
	const uint8_t *pointers; /* count u32 fields, little endian */
	uint32_t pointer_count;
	int hotkey;
	const uint8_t *text;
	size_t text_size;
} KpScrDialogText;
/* The BIN: each text in its place when it has its length, else appended with its pointers moved there; a hotkey's
 * first letter stored upper case. malloc'd, NULL when a text or pointer lies outside the skeleton. */
uint8_t *kp_scr_dialog(const uint8_t *skeleton, size_t size, const KpScrDialogText *texts, uint32_t count,
	size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif
