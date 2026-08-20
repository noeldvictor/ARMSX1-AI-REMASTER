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

struct see_engine {
    int enabled;
    int applied;
    int assets_written;
    char cache_root[PATH_MAX];
    char serial[32];
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
    char orig[PATH_MAX], gen[PATH_MAX], user[PATH_MAX], reverted[PATH_MAX], edited[PATH_MAX];
    snprintf(orig, sizeof(orig), "%s/orig.png", dir);
    snprintf(gen, sizeof(gen), "%s/generated.png", dir);
    snprintf(user, sizeof(user), "%s/user.png", dir);
    snprintf(reverted, sizeof(reverted), "%s/reverted", dir);
    snprintf(edited, sizeof(edited), "%s/edited", dir);
    if (!see_file_exists(orig)) {
        if (see_png_write(orig, rgb, w, h)) return 1;
        see->assets_written++;
    }
    int locked = see_file_exists(reverted) || see_file_exists(edited) || see_file_exists(user);
    if (see->enabled && !locked && !see_file_exists(gen)) {
        if (see_generate(rgb, w, h, gen)) return 1;
    }
    return 0;
}

void see_on_vram_write(see_engine_t* see, const uint16_t* vram,
                       unsigned x, unsigned y, unsigned w, unsigned h) {
    if (!see || !see->enabled || !vram) return;
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

void see_present_rgb(see_engine_t* see, uint8_t* rgb, int w, int h) {
    if (!see || !see->enabled || !rgb || w <= 0 || h <= 0) return;
    char hash[17];
    if (see_ingest_rgb(see, rgb, w, h, hash)) return;
    char dir[PATH_MAX];
    if (see_asset_dir(see, hash, dir, sizeof(dir))) return;
    char user[PATH_MAX], gen[PATH_MAX], reverted[PATH_MAX];
    snprintf(user, sizeof(user), "%s/user.png", dir);
    snprintf(gen, sizeof(gen), "%s/generated.png", dir);
    snprintf(reverted, sizeof(reverted), "%s/reverted", dir);
    if (see_file_exists(reverted)) return;
    const char* path = NULL;
    if (see_file_exists(user)) path = user;
    else if (see_file_exists(gen)) path = gen;
    if (!path) return;
    if (see_load_replacement(path, w, h, rgb) == 0)
        see->applied = 1;
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
        int has_gen = 0, has_user = 0;
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
        if (!first) fputs(",\n", man);
        first = 0;
        fprintf(man,
                "    {\"hash\": \"%s\", \"generated\": %s, \"user\": %s}",
                ent->d_name, has_gen ? "true" : "false", has_user ? "true" : "false");
    }
    closedir(d);
    fputs("\n  ]\n}\n", man);
    fclose(man);
    return rc;
}


