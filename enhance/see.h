#ifndef ARMSX_SEE_H
#define ARMSX_SEE_H

/*
 * Super Enhancement Engine — presentation layer.
 *
 * Hashes 2D surfaces (CPU-to-VRAM writes and the displayed framebuffer),
 * auto-applies an algorithmic upscale into a per-serial disk cache, and
 * composites replacements at present time. Never writes emulated VRAM.
 *
 * Draw order: user.png → generated.png → original pixels.
 * Pack export copies hashes + generated/user only (strips orig.png).
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct see_engine see_engine_t;

see_engine_t* see_create(void);
void see_destroy(see_engine_t* see);

void see_set_enabled(see_engine_t* see, int on);
int see_enabled(const see_engine_t* see);
void see_set_cache_root(see_engine_t* see, const char* path);
void see_set_serial(see_engine_t* see, const char* serial);
const char* see_serial(const see_engine_t* see);
int see_applied(const see_engine_t* see);
int see_asset_count(const see_engine_t* see);

/* Hash RGB8 (resolution mixed in) to a 16-char lowercase hex id. */
void see_hash_rgb(const uint8_t* rgb, int w, int h, char out[17]);

int see_png_write(const char* path, const uint8_t* rgb, int w, int h);
int see_png_read(const char* path, uint8_t** rgb, int* w, int* h);

/* Dump orig.png and, if enabled and unlocked, auto-generate generated.png. */
int see_ingest_rgb(see_engine_t* see, const uint8_t* rgb, int w, int h, char hash_out[17]);

/* CPU-to-VRAM completion. Reads VRAM, does not write it. */
void see_on_vram_write(see_engine_t* see, const uint16_t* vram,
                       unsigned x, unsigned y, unsigned w, unsigned h);

/*
 * If enabled, replace rgb in place with user/generated lookup of this frame's
 * hash (first sighting auto-generates). If disabled, rgb is left untouched.
 */
void see_present_rgb(see_engine_t* see, uint8_t* rgb, int w, int h);

/* Copy cache/<serial>/ to dest without orig.png; write manifest.json. */
int see_export_pack(const see_engine_t* see, const char* dest_dir);

#ifdef __cplusplus
}
#endif

#endif
