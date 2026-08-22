#ifndef ARMSX_SEE_H
#define ARMSX_SEE_H

/*
 * Super Enhancement Engine — presentation layer.
 *
 * Dumps 2D surfaces and 3D texture pages (CLUT-decoded) into
 * cache/<serial>/<hash>/orig.png. HD lives beside them as generated.png
 * or user.png — drop a PNG in that folder to tag the hash without
 * replaying. Never writes emulated VRAM.
 *
 * Draw order: xlat-<lang>.png → user.png → generated.png → original.
 * Drop xlat-en.png next to orig.png to translate without a replay.
 * Pack export copies hashes + generated/user/xlat only (strips orig.png).
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
void see_set_language(see_engine_t* see, const char* lang);
const char* see_language(const see_engine_t* see);
int see_applied(const see_engine_t* see);
int see_asset_count(const see_engine_t* see);

/* Write cache/<serial>/catalog.html — orig.png contact sheet for translators. */
int see_write_catalog(see_engine_t* see);

/* Hash RGB8 (resolution mixed in) to a 16-char lowercase hex id. */
void see_hash_rgb(const uint8_t* rgb, int w, int h, char out[17]);

int see_png_write(const char* path, const uint8_t* rgb, int w, int h);
int see_png_read(const char* path, uint8_t** rgb, int* w, int* h);

/* Dump orig.png and, if enabled and unlocked, auto-generate generated.png. */
int see_ingest_rgb(see_engine_t* see, const uint8_t* rgb, int w, int h, char hash_out[17]);

/* CPU-to-VRAM completion. Reads VRAM, does not write it. Dumps even if disabled. */
void see_on_vram_write(see_engine_t* see, const uint16_t* vram,
                       unsigned x, unsigned y, unsigned w, unsigned h);

/*
 * Texture-page dump on use (GP0 textured draw). Decodes 256×256 through the
 * CLUT, hashes the raw page+palette, writes orig.png + meta.json once.
 * If enabled, binds user/generated HD for see_replace_texel. Does not write VRAM.
 */
void see_on_texture_use(see_engine_t* see, const uint16_t* vram,
                        unsigned tpx, unsigned tpy, unsigned clutx, unsigned cluty,
                        int depth, unsigned texw_mx, unsigned texw_my,
                        unsigned texw_ox, unsigned texw_oy);

/* Sample the bound HD page at 8-bit UV. original is returned if nothing is bound. */
uint16_t see_replace_texel(see_engine_t* see, uint16_t tx, uint16_t ty,
                           unsigned tpx, unsigned tpy, unsigned clutx, unsigned cluty,
                           int depth, uint16_t original);

/* Generate generated.png for every dumped orig.png that is not locked. No replay. */
int see_enhance_cache(see_engine_t* see);

/*
 * If enabled, replace rgb in place with user/generated lookup of this frame's
 * hash (first sighting auto-generates). If disabled, still dumps orig.png
 * and leaves rgb untouched.
 */
void see_present_rgb(see_engine_t* see, uint8_t* rgb, int w, int h);

/* Copy cache/<serial>/ to dest without orig.png; write manifest.json. */
int see_export_pack(const see_engine_t* see, const char* dest_dir);

#ifdef __cplusplus
}
#endif

#endif
