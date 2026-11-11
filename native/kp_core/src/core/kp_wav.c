/* A WAV's header (kp_wav.h). */
#include "kp_wav.h"

#include <string.h>

#include "kp_kpf.h"

/* FindRiffChunk 0x49d7c0: the payload offset, size and what remains after the header; -1 not found or malformed, -2
 * when the header lies past the `have` bytes read. */
static int riff_find(const uint8_t *d, size_t have, size_t pos, uint64_t remaining, const char tag[4], size_t *at,
	uint32_t *size, uint64_t *left)
{
	for (;;) {
		uint32_t n;
		if (remaining < 8) return -1;
		if (pos + 8 > have) return -2;
		remaining -= 8;
		n = kp_le_u32(d + pos + 4);
		if (memcmp(d + pos, tag, 4) == 0) {
			*at = pos + 8;
			*size = n;
			*left = remaining;
			return 0;
		}
		if (n > remaining) return -1;
		remaining -= n;
		pos += 8 + (size_t)n; /* no even-size padding */
	}
}

int kp_wav_info(const uint8_t *d, size_t have, uint64_t total, KpWavInfo *w)
{
	size_t fmt_at, data_at;
	uint32_t fmt_size, data_size;
	uint64_t left, left2;
	int rc;
	memset(w, 0, sizeof *w);
	if (total < 12) return -1;
	if (have < 12) return -2;
	if (memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) return -1;
	if ((rc = riff_find(d, have, 12, total - 12, "fmt ", &fmt_at, &fmt_size, &left)) != 0) return rc;
	if (fmt_size < 16 || fmt_size > left) return -1;
	if (fmt_at + 16 > have) return -2;
	if ((rc = riff_find(d, have, fmt_at + fmt_size, left - fmt_size, "data", &data_at, &data_size, &left2)) != 0) return rc;
	if (data_size > left2) return -1;
	w->format_tag = (uint16_t)kp_le_u16(d + fmt_at);
	w->channels = (uint16_t)kp_le_u16(d + fmt_at + 2);
	w->sample_rate = kp_le_u32(d + fmt_at + 4);
	w->avg_bytes_per_sec = kp_le_u32(d + fmt_at + 8);
	w->block_align = (uint16_t)kp_le_u16(d + fmt_at + 12);
	w->bits_per_sample = (uint16_t)kp_le_u16(d + fmt_at + 14);
	w->data_offset = (uint32_t)data_at;
	w->data_length = data_size;
	return 0;
}
