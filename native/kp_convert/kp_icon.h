/*
 * The game's icon for the product's bundles (docs/phaseS.md, "Icons"; in C since ADR 0019, stage C1): the 32-bit
 * images of StarCraft.exe's icon group, made into each platform's icon files.
 *
 * The executable carries the icon at 16, 32, 48 and 64 pixels (an RT_GROUP_ICON of RGBA DIBs); the platforms want more
 * sizes than that (a favicon, a launcher's, a dock's), which are made from the largest image by a bilinear resampling
 * on premultiplied colour. The art is the original's: the files go to kp_convert's output (generated/icon/), never to
 * the repository.
 */
#ifndef KP_ICON_H
#define KP_ICON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The executable's main icon group (its second, group 106, is the cursor-sized one). */
#define KP_ICON_GROUP 102
/* The most images an icon group gives. */
#define KP_ICON_IMAGES 16

/* A square RGBA image, rows top down. */
typedef struct {
	int size;
	uint8_t *rgba;
} KpIconImage;

/* The 32-bit images of a PE32 executable's icon group, by their order in it (a later image of a size replaces an
 * earlier one): their count, or -1 with the reason (not a PE32 image, no such group, no 32-bit image). Free each
 * image's rgba. */
int kp_icon_read(const uint8_t *exe, size_t exe_size, int group, KpIconImage *images, int max, char *err, size_t err_size);
/* image (size n) at size pixels: bilinear on premultiplied colour, pixel centres aligned; malloc'd RGBA. */
uint8_t *kp_icon_resize(const uint8_t *rgba, int n, int size);
/* A PNG of 8-bit samples (channels 3: RGB, 4: RGBA), filter type 0 on every row, the rows deflated at level 6: malloc'd
 * bytes and *out_size, or NULL. */
uint8_t *kp_icon_png(const uint8_t *pixels, int width, int height, int channels, size_t *out_size);
/* Every platform's icon files from the images (kp_icon_read's) under out: icon-<size>.png, icon.ico, icon.icns, ios/,
 * web/ and android/res/. The count written (a file that holds those bytes already is left), or -1 with the reason. */
int kp_icon_write_set(const KpIconImage *images, int count, const char *out, int *files, char *err, size_t err_size);

#ifdef __cplusplus
}
#endif

#endif
