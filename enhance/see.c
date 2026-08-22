#include "enhance/see.h"

#include "miniz.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define SEE_MIN_PIXELS 256
#define SEE_MIN_VRAM_SIDE 16
#define SEE_SLOT_MAX 16

typedef struct {
    char hash[17];
    uint8_t* rgb;
    int w, h;
} see_slot_t;

struct see_engine {
    int enabled;
    int applied;
    int assets_written;
    char cache_root[PATH_MAX];
    char serial[32];
    char language[9];
    see_slot_t slots[SEE_SLOT_MAX];
    int nslots;
    uint8_t* bound_rgb;
    int bound_w, bound_h;
    unsigned bound_tpx, bound_tpy, bound_clutx, bound_cluty;
    int bound_depth;
    int bound_ok;
};

static int see_clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static uint8_t see_clampu8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

void see_hash_rgb(const uint8_t* rgb, int w, int h, char out[17]) {
    uint64_t hv = 1469598103934665603ULL;
    size_t n = (size_t)w * (size_t)h * 3u;
    for (size_t i = 0; i < n; i++) {
        hv ^= rgb[i];
        hv *= 1099511628211ULL;
    }
    hv ^= (uint64_t)w * 31u + (uint64_t)h;
    hv *= 1099511628211ULL;
    snprintf(out, 17, "%016llx", (unsigned long long)hv);
}

static void see_put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static int see_png_chunk(FILE* f, const char* tag, const uint8_t* data, uint32_t len) {
    uint8_t hdr[8];
    see_put32(hdr, len);
    memcpy(hdr + 4, tag, 4);
    if (fwrite(hdr, 1, 8, f) != 8) return 1;
    if (len && fwrite(data, 1, len, f) != len) return 1;
    mz_ulong crc = mz_crc32(MZ_CRC32_INIT, (const unsigned char*)tag, 4);
    if (len) crc = mz_crc32(crc, data, len);
    uint8_t crcb[4];
    see_put32(crcb, (uint32_t)crc);
    return fwrite(crcb, 1, 4, f) != 4;
}

int see_png_write(const char* path, const uint8_t* rgb, int w, int h) {
    if (!path || !rgb || w <= 0 || h <= 0) return 1;
    size_t raw_len = (size_t)h * (1 + (size_t)w * 3);
    uint8_t* raw = (uint8_t*)malloc(raw_len);
    if (!raw) return 1;
    for (int y = 0; y < h; y++) {
        uint8_t* dst = raw + (size_t)y * (1 + (size_t)w * 3);
        dst[0] = 0;
        memcpy(dst + 1, rgb + (size_t)y * w * 3, (size_t)w * 3);
    }
    mz_ulong cap = mz_compressBound((mz_ulong)raw_len);
    uint8_t* comp = (uint8_t*)malloc(cap);
    if (!comp) { free(raw); return 1; }
    if (mz_compress2(comp, &cap, raw, (mz_ulong)raw_len, MZ_DEFAULT_COMPRESSION) != MZ_OK) {
        free(raw); free(comp); return 1;
    }
    free(raw);
    FILE* f = fopen(path, "wb");
    if (!f) { free(comp); return 1; }
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    int bad = fwrite(sig, 1, 8, f) != 8;
    uint8_t ihdr[13];
    see_put32(ihdr + 0, (uint32_t)w);
    see_put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    bad |= see_png_chunk(f, "IHDR", ihdr, 13);
    bad |= see_png_chunk(f, "IDAT", comp, (uint32_t)cap);
    bad |= see_png_chunk(f, "IEND", NULL, 0);
    free(comp);
    fclose(f);
    return bad;
}

int see_png_read(const char* path, uint8_t** rgb, int* w, int* h) {
    *rgb = NULL;
    FILE* f = fopen(path, "rb");
    if (!f) return 1;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return 1; }
    long sz = ftell(f);
    if (sz < 33) { fclose(f); return 1; }
    if (fseek(f, 0, SEEK_SET)) { fclose(f); return 1; }
    uint8_t* data = (uint8_t*)malloc((size_t)sz);
    if (!data) { fclose(f); return 1; }
    if (fread(data, 1, (size_t)sz, f) != (size_t)sz) { free(data); fclose(f); return 1; }
    fclose(f);
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    if (memcmp(data, sig, 8) != 0) { free(data); return 1; }
    uint32_t width = 0, height = 0;
    uint8_t* idat = NULL;
    size_t idat_len = 0;
    size_t off = 8;
    while (off + 12 <= (size_t)sz) {
        uint32_t len = ((uint32_t)data[off] << 24) | ((uint32_t)data[off + 1] << 16) |
                       ((uint32_t)data[off + 2] << 8) | data[off + 3];
        const char* tag = (const char*)(data + off + 4);
        if (off + 12 + len > (size_t)sz) { free(data); free(idat); return 1; }
        const uint8_t* body = data + off + 8;
        if (!memcmp(tag, "IHDR", 4) && len >= 13) {
            width = ((uint32_t)body[0] << 24) | ((uint32_t)body[1] << 16) |
                    ((uint32_t)body[2] << 8) | body[3];
            height = ((uint32_t)body[4] << 24) | ((uint32_t)body[5] << 16) |
                     ((uint32_t)body[6] << 8) | body[7];
            if (body[8] != 8 || body[9] != 2) { free(data); free(idat); return 1; }
        } else if (!memcmp(tag, "IDAT", 4)) {
            uint8_t* nbuf = (uint8_t*)realloc(idat, idat_len + len);
            if (!nbuf) { free(data); free(idat); return 1; }
            idat = nbuf;
            memcpy(idat + idat_len, body, len);
            idat_len += len;
        } else if (!memcmp(tag, "IEND", 4)) {
            break;
        }
        off += 12 + len;
    }
    free(data);
    if (!width || !height || !idat) { free(idat); return 1; }
    size_t raw_len = (size_t)height * (1 + (size_t)width * 3);
    uint8_t* raw = (uint8_t*)malloc(raw_len);
    if (!raw) { free(idat); return 1; }
    mz_ulong dest = (mz_ulong)raw_len;
    if (mz_uncompress(raw, &dest, idat, (mz_ulong)idat_len) != MZ_OK || dest != raw_len) {
        free(raw); free(idat); return 1;
    }
    free(idat);
    uint8_t* out = (uint8_t*)malloc((size_t)width * height * 3);
    if (!out) { free(raw); return 1; }
    size_t stride = 1 + (size_t)width * 3;
    for (uint32_t y = 0; y < height; y++) {
        uint8_t filter = raw[(size_t)y * stride];
        const uint8_t* src = raw + (size_t)y * stride + 1;
        uint8_t* dst = out + (size_t)y * width * 3;
        if (filter == 0) {
            memcpy(dst, src, (size_t)width * 3);
        } else if (filter == 1) {
            for (uint32_t x = 0; x < width * 3; x++) {
                uint8_t left = (x >= 3) ? dst[x - 3] : 0;
                dst[x] = (uint8_t)(src[x] + left);
            }
        } else if (filter == 2) {
            const uint8_t* up = (y > 0) ? out + (size_t)(y - 1) * width * 3 : NULL;
            for (uint32_t x = 0; x < width * 3; x++)
                dst[x] = (uint8_t)(src[x] + (up ? up[x] : 0));
        } else {
            free(raw); free(out); return 1;
        }
    }
    free(raw);
    *rgb = out;
    *w = (int)width;
    *h = (int)height;
    return 0;
}

static int see_mkdirs(const char* path) {
    char buf[PATH_MAX];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(buf)) return 1;
    memcpy(buf, path, n + 1);
    for (char* p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(buf, 0755) && errno != EEXIST) return 1;
            *p = '/';
        }
    }
    if (mkdir(buf, 0755) && errno != EEXIST) return 1;
    return 0;
}

static int see_file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int see_has_xlat(const char* dir) {
    DIR* d = opendir(dir);
    if (!d) return 0;
    int found = 0;
    struct dirent* ent;
    while ((ent = readdir(d))) {
        if (strncmp(ent->d_name, "xlat-", 5) != 0) continue;
        size_t n = strlen(ent->d_name);
        if (n > 9 && !strcmp(ent->d_name + n - 4, ".png")) {
            found = 1;
            break;
        }
    }
    closedir(d);
    return found;
}

static int see_asset_locked(const char* dir) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/reverted", dir);
    if (see_file_exists(path)) return 1;
    snprintf(path, sizeof(path), "%s/edited", dir);
    if (see_file_exists(path)) return 1;
    snprintf(path, sizeof(path), "%s/user.png", dir);
    if (see_file_exists(path)) return 1;
    return see_has_xlat(dir);
}

static int see_xlat_path(const see_engine_t* see, const char* dir, char* out, size_t n) {
    if (!see || !see->language[0] || !dir) return 1;
    int wr = snprintf(out, n, "%s/xlat-%s.png", dir, see->language);
    return (wr < 0 || (size_t)wr >= n);
}

/* xlat-<lang>.png → user.png → generated.png */
static const char* see_pick_replacement(const see_engine_t* see, const char* dir,
                                        char* user, char* gen, char* xlat) {
    if (see_xlat_path(see, dir, xlat, PATH_MAX) == 0 && see_file_exists(xlat))
        return xlat;
    snprintf(user, PATH_MAX, "%s/user.png", dir);
    if (see_file_exists(user)) return user;
    snprintf(gen, PATH_MAX, "%s/generated.png", dir);
    if (see_file_exists(gen)) return gen;
    return NULL;
}

static void see_serial_or_unknown(const see_engine_t* see, char* out, size_t n);

int see_write_catalog(see_engine_t* see) {
    if (!see) return 1;
    char serial[32], root[PATH_MAX], path[PATH_MAX];
    see_serial_or_unknown(see, serial, sizeof(serial));
    snprintf(root, sizeof(root), "%s/%s", see->cache_root, serial);
    if (see_mkdirs(root)) return 1;
    snprintf(path, sizeof(path), "%s/catalog.html", root);
    FILE* f = fopen(path, "wb");
    if (!f) return 1;
    fprintf(f,
            "<!DOCTYPE html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
            "<title>%s translation catalog</title>\n"
            "<style>body{font:14px sans-serif;background:#111;color:#eee;margin:24px}"
            "a{color:#8cf}img{image-rendering:pixelated;max-width:256px;background:#000}"
            ".card{display:inline-block;vertical-align:top;margin:8px;padding:8px;"
            "background:#1c1c1c;border:1px solid #333}code{color:#fd6}</style>"
            "</head><body>\n<h1>%s</h1>\n"
            "<p>Open <code>orig.png</code> in any image editor. Save your translation "
            "as <code>xlat-en.png</code> (or <code>xlat-XX.png</code>) in the same "
            "folder. Hash is the tag &mdash; no replay. "
            "<code>user.png</code> is HD touch-up; xlat wins when a language is set.</p>\n",
            serial, serial);
    DIR* d = opendir(root);
    int n = 0;
    if (d) {
        struct dirent* ent;
        while ((ent = readdir(d))) {
            if (ent->d_name[0] == '.') continue;
            char dir[PATH_MAX], orig[PATH_MAX], meta[PATH_MAX];
            snprintf(dir, sizeof(dir), "%s/%s", root, ent->d_name);
            struct stat st;
            if (stat(dir, &st) || !S_ISDIR(st.st_mode)) continue;
            snprintf(orig, sizeof(orig), "%s/orig.png", dir);
            if (!see_file_exists(orig)) continue;
            const char* kind = "surface";
            snprintf(meta, sizeof(meta), "%s/meta.json", dir);
            if (see_file_exists(meta)) kind = "texpage";
            int has_xlat = see_has_xlat(dir);
            fprintf(f,
                    "<div class=\"card\"><img src=\"%s/orig.png\" alt=\"%s\">"
                    "<div><code>%s</code><br>%s%s</div>"
                    "<div>save <code>xlat-en.png</code> here</div></div>\n",
                    ent->d_name, ent->d_name, ent->d_name, kind,
                    has_xlat ? " · translated" : "");
            n++;
        }
        closedir(d);
    }
    fprintf(f, "<p>%d dumped assets.</p>\n</body></html>\n", n);
    fclose(f);
    return 0;
}

static void see_serial_or_unknown(const see_engine_t* see, char* out, size_t n) {
    if (see->serial[0]) snprintf(out, n, "%s", see->serial);
    else snprintf(out, n, "UNKNOWN");
}

static int see_asset_dir(const see_engine_t* see, const char* hash, char* out, size_t n) {
    char serial[32];
    see_serial_or_unknown(see, serial, sizeof(serial));
    int wr = snprintf(out, n, "%s/%s/%s", see->cache_root, serial, hash);
    return (wr < 0 || (size_t)wr >= n);
}

static uint8_t* see_unsharp(const uint8_t* src, int w, int h) {
    uint8_t* dst = (uint8_t*)malloc((size_t)w * h * 3);
    if (!dst) return NULL;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int sr = 0, sg = 0, sb = 0, count = 0;
            for (int dy = -1; dy <= 1; dy++) {
                int yy = see_clampi(y + dy, 0, h - 1);
                for (int dx = -1; dx <= 1; dx++) {
                    int xx = see_clampi(x + dx, 0, w - 1);
                    const uint8_t* p = src + ((size_t)yy * w + xx) * 3;
                    sr += p[0]; sg += p[1]; sb += p[2];
                    count++;
                }
            }
            const uint8_t* s = src + ((size_t)y * w + x) * 3;
            uint8_t* d = dst + ((size_t)y * w + x) * 3;
            int br = sr / count, bg = sg / count, bb = sb / count;
            d[0] = see_clampu8(s[0] * 2 - br);
            d[1] = see_clampu8(s[1] * 2 - bg);
            d[2] = see_clampu8(s[2] * 2 - bb);
        }
    }
    return dst;
}

static int see_same_px(const uint8_t* a, const uint8_t* b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

static void see_copy_px(uint8_t* d, const uint8_t* s) {
    d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
}

static uint8_t* see_scale2x(const uint8_t* src, int w, int h) {
    int ow = w * 2, oh = h * 2;
    uint8_t* dst = (uint8_t*)malloc((size_t)ow * oh * 3);
    if (!dst) return NULL;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t* e = src + ((size_t)y * w + x) * 3;
            const uint8_t* b = src + ((size_t)see_clampi(y - 1, 0, h - 1) * w + x) * 3;
            const uint8_t* hpx = src + ((size_t)see_clampi(y + 1, 0, h - 1) * w + x) * 3;
            const uint8_t* d = src + ((size_t)y * w + see_clampi(x - 1, 0, w - 1)) * 3;
            const uint8_t* f = src + ((size_t)y * w + see_clampi(x + 1, 0, w - 1)) * 3;
            uint8_t* e0 = dst + ((size_t)(y * 2) * ow + (x * 2)) * 3;
            uint8_t* e1 = dst + ((size_t)(y * 2) * ow + (x * 2 + 1)) * 3;
            uint8_t* e2 = dst + ((size_t)(y * 2 + 1) * ow + (x * 2)) * 3;
            uint8_t* e3 = dst + ((size_t)(y * 2 + 1) * ow + (x * 2 + 1)) * 3;
            int bd = see_same_px(b, d), bf = see_same_px(b, f);
            int dh = see_same_px(d, hpx), fh = see_same_px(f, hpx);
            see_copy_px(e0, (bd && !bf && !dh) ? d : e);
            see_copy_px(e1, (bf && !bd && !fh) ? f : e);
            see_copy_px(e2, (dh && !bd && !fh) ? d : e);
            see_copy_px(e3, (fh && !bf && !dh) ? f : e);
        }
    }
    return dst;
}

static uint8_t* see_scale_to(const uint8_t* src, int sw, int sh, int dw, int dh) {
    uint8_t* dst = (uint8_t*)malloc((size_t)dw * dh * 3);
    if (!dst) return NULL;
    if (sw == dw && sh == dh) {
        memcpy(dst, src, (size_t)dw * dh * 3);
        return dst;
    }
    if (sw == dw * 2 && sh == dh * 2) {
        for (int y = 0; y < dh; y++) {
            for (int x = 0; x < dw; x++) {
                const uint8_t* a = src + ((size_t)(y * 2) * sw + (x * 2)) * 3;
                const uint8_t* b = src + ((size_t)(y * 2) * sw + (x * 2 + 1)) * 3;
                const uint8_t* c = src + ((size_t)(y * 2 + 1) * sw + (x * 2)) * 3;
                const uint8_t* d = src + ((size_t)(y * 2 + 1) * sw + (x * 2 + 1)) * 3;
                uint8_t* o = dst + ((size_t)y * dw + x) * 3;
                o[0] = (uint8_t)(((int)a[0] + b[0] + c[0] + d[0]) / 4);
                o[1] = (uint8_t)(((int)a[1] + b[1] + c[1] + d[1]) / 4);
                o[2] = (uint8_t)(((int)a[2] + b[2] + c[2] + d[2]) / 4);
            }
        }
        return dst;
    }
    for (int y = 0; y < dh; y++) {
        int sy = see_clampi(y * sh / dh, 0, sh - 1);
        for (int x = 0; x < dw; x++) {
            int sx = see_clampi(x * sw / dw, 0, sw - 1);
            see_copy_px(dst + ((size_t)y * dw + x) * 3, src + ((size_t)sy * sw + sx) * 3);
        }
    }
    return dst;
}

static int see_generate(const uint8_t* src, int w, int h, const char* gen_path) {
    uint8_t* sharp = see_unsharp(src, w, h);
    if (!sharp) return 1;
    uint8_t* hi = see_scale2x(sharp, w, h);
    free(sharp);
    if (!hi) return 1;
    int rc = see_png_write(gen_path, hi, w * 2, h * 2);
    free(hi);
    return rc;
}

static int see_load_replacement(const char* path, int dw, int dh, uint8_t* dst) {
    uint8_t* img = NULL;
    int iw = 0, ih = 0;
    if (see_png_read(path, &img, &iw, &ih)) return 1;
    uint8_t* scaled = see_scale_to(img, iw, ih, dw, dh);
    free(img);
    if (!scaled) return 1;
    memcpy(dst, scaled, (size_t)dw * dh * 3);
    free(scaled);
    return 0;
}

see_engine_t* see_create(void) {
    see_engine_t* see = (see_engine_t*)calloc(1, sizeof(*see));
    if (!see) return NULL;
    snprintf(see->cache_root, sizeof(see->cache_root), "cache");
    return see;
}

void see_destroy(see_engine_t* see) {
    if (!see) return;
    for (int i = 0; i < see->nslots; i++)
        free(see->slots[i].rgb);
    free(see->bound_rgb);
    free(see);
}

void see_set_enabled(see_engine_t* see, int on) {
    if (see) see->enabled = on ? 1 : 0;
}

int see_enabled(const see_engine_t* see) {
    return see && see->enabled;
}

void see_set_cache_root(see_engine_t* see, const char* path) {
    if (!see || !path || !path[0]) return;
    snprintf(see->cache_root, sizeof(see->cache_root), "%s", path);
}

void see_set_language(see_engine_t* see, const char* lang) {
    if (!see) return;
    memset(see->language, 0, sizeof(see->language));
    if (!lang) return;
    size_t j = 0;
    for (size_t i = 0; lang[i] && j + 1 < sizeof(see->language); i++) {
        unsigned char c = (unsigned char)lang[i];
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            see->language[j++] = (char)c;
    }
}

const char* see_language(const see_engine_t* see) {
    return see ? see->language : "";
}

void see_set_serial(see_engine_t* see, const char* serial) {
    if (!see) return;
    see->serial[0] = 0;
    if (!serial) return;
    size_t o = 0;
    for (const char* p = serial; *p && o + 1 < sizeof(see->serial); p++) {
        char c = *p;
        if (c == '_') c = '-';
        if (c == '\\' || c == '/' || c == ';') break;
        see->serial[o++] = (char)toupper((unsigned char)c);
    }
    see->serial[o] = 0;
}

const char* see_serial(const see_engine_t* see) {
    return see ? see->serial : "";
}

int see_applied(const see_engine_t* see) {
    return see ? see->applied : 0;
}

int see_asset_count(const see_engine_t* see) {
    return see ? see->assets_written : 0;
}

int see_ingest_rgb(see_engine_t* see, const uint8_t* rgb, int w, int h, char hash_out[17]) {
    if (!see || !rgb || w <= 0 || h <= 0) return 1;
    if ((size_t)w * (size_t)h < SEE_MIN_PIXELS) return 1;
    char hash[17];
    see_hash_rgb(rgb, w, h, hash);
    if (hash_out) memcpy(hash_out, hash, 17);
    char dir[PATH_MAX];
    if (see_asset_dir(see, hash, dir, sizeof(dir))) return 1;
    if (see_mkdirs(dir)) return 1;
    char orig[PATH_MAX], gen[PATH_MAX];
    snprintf(orig, sizeof(orig), "%s/orig.png", dir);
    snprintf(gen, sizeof(gen), "%s/generated.png", dir);
    if (!see_file_exists(orig)) {
        if (see_png_write(orig, rgb, w, h)) return 1;
        see->assets_written++;
        see_write_catalog(see);
    }
    int locked = see_asset_locked(dir);
    if (see->enabled && !locked && !see_file_exists(gen)) {
        if (see_generate(rgb, w, h, gen)) return 1;
    }
    return 0;
}

static uint16_t see_fetch_texel(
    const uint16_t* vram, uint16_t tx, uint16_t ty,
    unsigned tpx, unsigned tpy, unsigned clutx, unsigned cluty, int depth,
    unsigned texw_mx, unsigned texw_my, unsigned texw_ox, unsigned texw_oy
) {
    tx = (uint16_t)((tx & ~texw_mx) | (texw_ox & texw_mx));
    ty = (uint16_t)((ty & ~texw_my) | (texw_oy & texw_my));
    tx &= 0xff;
    ty &= 0xff;
    if (depth == 0) {
        uint16_t packed = vram[(tpx + (tx >> 2)) + ((tpy + ty) * 1024)];
        int index = (packed >> ((tx & 0x3) << 2)) & 0xf;
        return vram[(clutx + (unsigned)index) + (cluty * 1024)];
    }
    if (depth == 1) {
        uint16_t packed = vram[(tpx + (tx >> 1)) + ((tpy + ty) * 1024)];
        int index = (packed >> ((tx & 0x1) << 3)) & 0xff;
        return vram[(clutx + (unsigned)index) + (cluty * 1024)];
    }
    return vram[(tpx + tx) + ((tpy + ty) * 1024)];
}

static void see_hash_texpage(
    const uint16_t* vram,
    unsigned tpx, unsigned tpy, unsigned clutx, unsigned cluty, int depth,
    unsigned texw_mx, unsigned texw_my, unsigned texw_ox, unsigned texw_oy,
    char out[17]
) {
    uint64_t hv = 1469598103934665603ULL;
    unsigned words[] = { tpx, tpy, clutx, cluty, (unsigned)depth,
                         texw_mx, texw_my, texw_ox, texw_oy };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        hv ^= words[i];
        hv *= 1099511628211ULL;
    }
    unsigned pw = depth == 0 ? 64u : (depth == 1 ? 128u : 256u);
    unsigned clutn = depth == 0 ? 16u : (depth == 1 ? 256u : 0u);
    for (unsigned y = 0; y < 256; y++) {
        for (unsigned x = 0; x < pw; x++) {
            uint16_t p = vram[((tpx + x) & 0x3ff) + (((tpy + y) & 0x1ff) * 1024)];
            hv ^= p;
            hv *= 1099511628211ULL;
        }
    }
    for (unsigned i = 0; i < clutn; i++) {
        uint16_t p = vram[((clutx + i) & 0x3ff) + ((cluty & 0x1ff) * 1024)];
        hv ^= p;
        hv *= 1099511628211ULL;
    }
    snprintf(out, 17, "%016llx", (unsigned long long)hv);
}

static void see_bgr555_to_rgb(uint16_t p, uint8_t* d) {
    uint8_t r = (uint8_t)(p & 0x1f);
    uint8_t g = (uint8_t)((p >> 5) & 0x1f);
    uint8_t b = (uint8_t)((p >> 10) & 0x1f);
    d[0] = (uint8_t)((r << 3) | (r >> 2));
    d[1] = (uint8_t)((g << 3) | (g >> 2));
    d[2] = (uint8_t)((b << 3) | (b >> 2));
}

static uint16_t see_rgb_to_bgr555(const uint8_t* s) {
    return (uint16_t)((s[0] >> 3) | ((s[1] >> 3) << 5) | ((s[2] >> 3) << 10));
}

static int see_write_meta(const char* dir, const char* hash, int depth,
                          unsigned tpx, unsigned tpy, unsigned clutx, unsigned cluty) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/meta.json", dir);
    if (see_file_exists(path)) return 0;
    FILE* f = fopen(path, "wb");
    if (!f) return 1;
    fprintf(f,
            "{\n"
            "  \"kind\": \"texpage\",\n"
            "  \"hash\": \"%s\",\n"
            "  \"w\": 256,\n"
            "  \"h\": 256,\n"
            "  \"depth\": %d,\n"
            "  \"tpx\": %u,\n"
            "  \"tpy\": %u,\n"
            "  \"clutx\": %u,\n"
            "  \"cluty\": %u\n"
            "}\n",
            hash, depth, tpx, tpy, clutx, cluty);
    fclose(f);
    return 0;
}

static int see_append_index(const see_engine_t* see, const char* hash, const char* kind) {
    char serial[32], path[PATH_MAX];
    see_serial_or_unknown(see, serial, sizeof(serial));
    snprintf(path, sizeof(path), "%s/%s/dumps.jsonl", see->cache_root, serial);
    FILE* f = fopen(path, "ab");
    if (!f) return 1;
    fprintf(f, "{\"hash\":\"%s\",\"kind\":\"%s\"}\n", hash, kind);
    fclose(f);
    return 0;
}

static int see_bind_hd(see_engine_t* see, const char* dir,
                       unsigned tpx, unsigned tpy, unsigned clutx, unsigned cluty, int depth) {
    char user[PATH_MAX], gen[PATH_MAX], xlat[PATH_MAX];
    const char* path = see_pick_replacement(see, dir, user, gen, xlat);
    see->bound_ok = 0;
    free(see->bound_rgb);
    see->bound_rgb = NULL;
    if (!path) return 1;
    uint8_t* img = NULL;
    int iw = 0, ih = 0;
    if (see_png_read(path, &img, &iw, &ih) || !img || iw <= 0 || ih <= 0) {
        free(img);
        return 1;
    }
    see->bound_rgb = img;
    see->bound_w = iw;
    see->bound_h = ih;
    see->bound_tpx = tpx;
    see->bound_tpy = tpy;
    see->bound_clutx = clutx;
    see->bound_cluty = cluty;
    see->bound_depth = depth;
    see->bound_ok = 1;
    return 0;
}

void see_on_texture_use(see_engine_t* see, const uint16_t* vram,
                        unsigned tpx, unsigned tpy, unsigned clutx, unsigned cluty,
                        int depth, unsigned texw_mx, unsigned texw_my,
                        unsigned texw_ox, unsigned texw_oy) {
    if (!see || !vram) return;
    char hash[17];
    see_hash_texpage(vram, tpx, tpy, clutx, cluty, depth,
                     texw_mx, texw_my, texw_ox, texw_oy, hash);
    char dir[PATH_MAX];
    if (see_asset_dir(see, hash, dir, sizeof(dir))) return;
    if (see_mkdirs(dir)) return;
    char orig[PATH_MAX];
    snprintf(orig, sizeof(orig), "%s/orig.png", dir);
    if (!see_file_exists(orig)) {
        uint8_t* rgb = (uint8_t*)malloc(256u * 256u * 3u);
        if (!rgb) return;
        for (int ty = 0; ty < 256; ty++) {
            for (int tx = 0; tx < 256; tx++) {
                uint16_t p = see_fetch_texel(
                    vram, (uint16_t)tx, (uint16_t)ty, tpx, tpy, clutx, cluty, depth,
                    texw_mx, texw_my, texw_ox, texw_oy
                );
                see_bgr555_to_rgb(p, rgb + ((size_t)ty * 256 + (size_t)tx) * 3);
            }
        }
        if (see_png_write(orig, rgb, 256, 256) == 0) {
            see->assets_written++;
            see_write_meta(dir, hash, depth, tpx, tpy, clutx, cluty);
            see_append_index(see, hash, "texpage");
            see_write_catalog(see);
            if (see->enabled) {
                char gen[PATH_MAX];
                snprintf(gen, sizeof(gen), "%s/generated.png", dir);
                if (!see_asset_locked(dir) && !see_file_exists(gen))
                    see_generate(rgb, 256, 256, gen);
            }
        }
        free(rgb);
    }
    if (see->enabled)
        see_bind_hd(see, dir, tpx, tpy, clutx, cluty, depth);
}

uint16_t see_replace_texel(see_engine_t* see, uint16_t tx, uint16_t ty,
                           unsigned tpx, unsigned tpy, unsigned clutx, unsigned cluty,
                           int depth, uint16_t original) {
    if (!see || !see->enabled || !see->bound_ok || !see->bound_rgb || !original)
        return original;
    if (see->bound_tpx != tpx || see->bound_tpy != tpy ||
        see->bound_clutx != clutx || see->bound_cluty != cluty ||
        see->bound_depth != depth)
        return original;
    tx &= 0xff;
    ty &= 0xff;
    int sx = (int)tx * see->bound_w / 256;
    int sy = (int)ty * see->bound_h / 256;
    sx = see_clampi(sx, 0, see->bound_w - 1);
    sy = see_clampi(sy, 0, see->bound_h - 1);
    const uint8_t* p = see->bound_rgb + ((size_t)sy * (size_t)see->bound_w + (size_t)sx) * 3;
    return see_rgb_to_bgr555(p) | (original & 0x8000);
}

int see_enhance_cache(see_engine_t* see) {
    if (!see) return -1;
    char serial[32], root[PATH_MAX];
    see_serial_or_unknown(see, serial, sizeof(serial));
    snprintf(root, sizeof(root), "%s/%s", see->cache_root, serial);
    DIR* d = opendir(root);
    if (!d) return 0;
    int made = 0;
    struct dirent* ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        char dir[PATH_MAX];
        snprintf(dir, sizeof(dir), "%s/%s", root, ent->d_name);
        struct stat st;
        if (stat(dir, &st) || !S_ISDIR(st.st_mode)) continue;
        char orig[PATH_MAX], gen[PATH_MAX];
        snprintf(orig, sizeof(orig), "%s/orig.png", dir);
        snprintf(gen, sizeof(gen), "%s/generated.png", dir);
        if (!see_file_exists(orig)) continue;
        if (see_asset_locked(dir)) continue;
        if (see_file_exists(gen)) continue;
        uint8_t* rgb = NULL;
        int w = 0, h = 0;
        if (see_png_read(orig, &rgb, &w, &h) || !rgb) {
            free(rgb);
            continue;
        }
        if (see_generate(rgb, w, h, gen) == 0)
            made++;
        free(rgb);
    }
    closedir(d);
    return made;
}

void see_on_vram_write(see_engine_t* see, const uint16_t* vram,
                       unsigned x, unsigned y, unsigned w, unsigned h) {
    if (!see || !vram) return;
    if (w < SEE_MIN_VRAM_SIDE || h < SEE_MIN_VRAM_SIDE) return;
    if ((size_t)w * (size_t)h < SEE_MIN_PIXELS) return;
    uint8_t* rgb = (uint8_t*)malloc((size_t)w * h * 3);
    if (!rgb) return;
    for (unsigned yy = 0; yy < h; yy++) {
        unsigned vy = (y + yy) & 0x1ff;
        for (unsigned xx = 0; xx < w; xx++) {
            unsigned vx = (x + xx) & 0x3ff;
            uint16_t p = vram[vx + vy * 1024];
            uint8_t r = (uint8_t)(p & 0x1f);
            uint8_t g = (uint8_t)((p >> 5) & 0x1f);
            uint8_t b = (uint8_t)((p >> 10) & 0x1f);
            uint8_t* d = rgb + ((size_t)yy * w + xx) * 3;
            d[0] = (uint8_t)((r << 3) | (r >> 2));
            d[1] = (uint8_t)((g << 3) | (g >> 2));
            d[2] = (uint8_t)((b << 3) | (b >> 2));
        }
    }
    see_ingest_rgb(see, rgb, (int)w, (int)h, NULL);
    free(rgb);
}

static void see_slot_store(see_engine_t* see, const char* hash, const uint8_t* rgb, int w, int h) {
    size_t nbytes = (size_t)w * (size_t)h * 3u;
    see_slot_t* slot = NULL;
    for (int i = 0; i < see->nslots; i++) {
        if (!memcmp(see->slots[i].hash, hash, 17)) {
            slot = &see->slots[i];
            break;
        }
    }
    if (!slot) {
        if (see->nslots == SEE_SLOT_MAX) {
            free(see->slots[0].rgb);
            memmove(&see->slots[0], &see->slots[1], sizeof(see_slot_t) * (SEE_SLOT_MAX - 1));
            see->nslots = SEE_SLOT_MAX - 1;
            memset(&see->slots[see->nslots], 0, sizeof(see_slot_t));
        }
        slot = &see->slots[see->nslots++];
        memcpy(slot->hash, hash, 17);
        slot->rgb = NULL;
    }
    uint8_t* copy = (uint8_t*)realloc(slot->rgb, nbytes);
    if (!copy) return;
    memcpy(copy, rgb, nbytes);
    slot->rgb = copy;
    slot->w = w;
    slot->h = h;
}

static const uint8_t* see_slot_find(const see_engine_t* see, const char* hash, int w, int h) {
    for (int i = 0; i < see->nslots; i++) {
        if (!memcmp(see->slots[i].hash, hash, 17) &&
            see->slots[i].w == w && see->slots[i].h == h)
            return see->slots[i].rgb;
    }
    return NULL;
}

void see_present_rgb(see_engine_t* see, uint8_t* rgb, int w, int h) {
    if (!see || !rgb || w <= 0 || h <= 0) return;
    char hash[17];
    if (see_ingest_rgb(see, rgb, w, h, hash)) return;
    if (!see->enabled) return;
    char dir[PATH_MAX];
    if (see_asset_dir(see, hash, dir, sizeof(dir))) return;
    char reverted[PATH_MAX], user[PATH_MAX], gen[PATH_MAX], xlat[PATH_MAX];
    snprintf(reverted, sizeof(reverted), "%s/reverted", dir);
    if (see_file_exists(reverted)) return;
    const char* path = see_pick_replacement(see, dir, user, gen, xlat);
    if (!path) return;

    size_t nbytes = (size_t)w * (size_t)h * 3u;
    uint8_t* tmp = (uint8_t*)malloc(nbytes);
    if (!tmp) return;
    if (see_load_replacement(path, w, h, tmp) == 0) {
        see_slot_store(see, hash, tmp, w, h);
        memcpy(rgb, tmp, nbytes);
        see->applied = 1;
    } else {
        /* Truncated/malformed PNG: keep the last good replacement, never garbage. */
        const uint8_t* prev = see_slot_find(see, hash, w, h);
        if (prev) {
            memcpy(rgb, prev, nbytes);
            see->applied = 1;
        }
    }
    free(tmp);
}

static int see_copy_file(const char* src, const char* dst) {
    FILE* in = fopen(src, "rb");
    if (!in) return 1;
    FILE* out = fopen(dst, "wb");
    if (!out) { fclose(in); return 1; }
    char buf[8192];
    size_t n;
    int bad = 0;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { bad = 1; break; }
    }
    fclose(in);
    fclose(out);
    return bad;
}

int see_export_pack(const see_engine_t* see, const char* dest_dir) {
    if (!see || !dest_dir) return 1;
    char serial[32];
    see_serial_or_unknown(see, serial, sizeof(serial));
    char src_root[PATH_MAX];
    snprintf(src_root, sizeof(src_root), "%s/%s", see->cache_root, serial);
    if (see_mkdirs(dest_dir)) return 1;
    DIR* d = opendir(src_root);
    if (!d) return 1;
    char man_path[PATH_MAX];
    snprintf(man_path, sizeof(man_path), "%s/manifest.json", dest_dir);
    FILE* man = fopen(man_path, "wb");
    if (!man) { closedir(d); return 1; }
    fprintf(man, "{\n  \"serial\": \"%s\",\n  \"assets\": [\n", serial);
    int first = 1, rc = 0;
    struct dirent* ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        char asset_src[PATH_MAX];
        snprintf(asset_src, sizeof(asset_src), "%s/%s", src_root, ent->d_name);
        struct stat st;
        if (stat(asset_src, &st) || !S_ISDIR(st.st_mode)) continue;
        char asset_dst[PATH_MAX];
        snprintf(asset_dst, sizeof(asset_dst), "%s/%s", dest_dir, ent->d_name);
        if (see_mkdirs(asset_dst)) { rc = 1; break; }
        const char* names[] = { "generated.png", "user.png", "reverted", "edited", NULL };
        int has_gen = 0, has_user = 0, has_xlat = 0;
        for (int i = 0; names[i]; i++) {
            char from[PATH_MAX], to[PATH_MAX];
            snprintf(from, sizeof(from), "%s/%s", asset_src, names[i]);
            if (!see_file_exists(from)) continue;
            snprintf(to, sizeof(to), "%s/%s", asset_dst, names[i]);
            if (see_copy_file(from, to)) { rc = 1; break; }
            if (!strcmp(names[i], "generated.png")) has_gen = 1;
            if (!strcmp(names[i], "user.png")) has_user = 1;
        }
        if (rc) break;
        DIR* ad = opendir(asset_src);
        if (ad) {
            struct dirent* ae;
            while ((ae = readdir(ad))) {
                if (strncmp(ae->d_name, "xlat-", 5) != 0) continue;
                size_t ln = strlen(ae->d_name);
                if (ln < 10 || strcmp(ae->d_name + ln - 4, ".png")) continue;
                char from[PATH_MAX], to[PATH_MAX];
                snprintf(from, sizeof(from), "%s/%s", asset_src, ae->d_name);
                snprintf(to, sizeof(to), "%s/%s", asset_dst, ae->d_name);
                if (see_copy_file(from, to)) { rc = 1; break; }
                has_xlat = 1;
            }
            closedir(ad);
        }
        if (rc) break;
        if (!first) fputs(",\n", man);
        first = 0;
        fprintf(man,
                "    {\"hash\": \"%s\", \"generated\": %s, \"user\": %s, \"xlat\": %s}",
                ent->d_name, has_gen ? "true" : "false", has_user ? "true" : "false",
                has_xlat ? "true" : "false");
    }
    closedir(d);
    fputs("\n  ]\n}\n", man);
    fclose(man);
    return rc;
}


