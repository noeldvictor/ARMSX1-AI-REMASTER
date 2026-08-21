#include "enhance/see.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static void fill_checker(uint8_t* rgb, int w, int h, int tile) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int on = ((x / tile) ^ (y / tile)) & 1;
            uint8_t* p = rgb + ((size_t)y * w + x) * 3;
            p[0] = on ? 220 : 20;
            p[1] = on ? 40 : 180;
            p[2] = on ? 90 : 30;
        }
    }
}

static int all_rgb(const uint8_t* rgb, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; i++) {
        if (rgb[i * 3] != r || rgb[i * 3 + 1] != g || rgb[i * 3 + 2] != b)
            return 0;
    }
    return 1;
}

int main(void) {
    const char* root = "build/tests/see-work/rep";
    mkdir("build", 0755);
    mkdir("build/tests", 0755);
    mkdir("build/tests/see-work", 0755);
    mkdir(root, 0755);

    see_engine_t* see = see_create();
    if (!see) {
        fprintf(stderr, "SEE_REPLACEMENT failed reason=alloc\n");
        return 1;
    }
    see_set_cache_root(see, root);
    see_set_serial(see, "TEST-000.00");

    const int w = 32, h = 32;
    uint8_t orig[32 * 32 * 3];
    uint8_t frame[32 * 32 * 3];
    fill_checker(orig, w, h, 4);

    /* Off: present must be a no-op. */
    memcpy(frame, orig, sizeof(frame));
    see_set_enabled(see, 0);
    see_present_rgb(see, frame, w, h);
    if (memcmp(frame, orig, sizeof(frame)) != 0) {
        fprintf(stderr, "SEE_REPLACEMENT failed reason=off-mutated\n");
        see_destroy(see);
        return 1;
    }
    printf("SEE_REPLACEMENT passed case=off-passthrough\n");

    /* On: first sighting auto-generates and the presented pixels change. */
    memcpy(frame, orig, sizeof(frame));
    see_set_enabled(see, 1);
    char hash[17];
    if (see_ingest_rgb(see, orig, w, h, hash)) {
        fprintf(stderr, "SEE_REPLACEMENT failed reason=ingest\n");
        see_destroy(see);
        return 1;
    }
    see_present_rgb(see, frame, w, h);
    if (memcmp(frame, orig, sizeof(frame)) == 0) {
        fprintf(stderr, "SEE_REPLACEMENT failed reason=on-identical hash=%s\n", hash);
        see_destroy(see);
        return 1;
    }
    printf("SEE_REPLACEMENT passed case=on-auto-upscale hash=%s\n", hash);

    /* Disable and restore: original texels again. */
    memcpy(frame, orig, sizeof(frame));
    see_set_enabled(see, 0);
    see_present_rgb(see, frame, w, h);
    if (memcmp(frame, orig, sizeof(frame)) != 0) {
        fprintf(stderr, "SEE_REPLACEMENT failed reason=disable-did-not-restore\n");
        see_destroy(see);
        return 1;
    }
    printf("SEE_REPLACEMENT passed case=disable-original\n");

    /* user.png wins over generated. */
    char user_path[512];
    snprintf(user_path, sizeof(user_path), "%s/TEST-000.00/%s/user.png", root, hash);
    uint8_t user[32 * 32 * 3];
    for (int i = 0; i < w * h; i++) {
        user[i * 3 + 0] = 16;
        user[i * 3 + 1] = 64;
        user[i * 3 + 2] = 200;
    }
    if (see_png_write(user_path, user, w, h)) {
        fprintf(stderr, "SEE_REPLACEMENT failed reason=user-write\n");
        see_destroy(see);
        return 1;
    }
    memcpy(frame, orig, sizeof(frame));
    see_set_enabled(see, 1);
    see_present_rgb(see, frame, w, h);
    if (!all_rgb(frame, w, h, 16, 64, 200)) {
        fprintf(stderr, "SEE_REPLACEMENT failed reason=user-did-not-win\n");
        see_destroy(see);
        return 1;
    }
    printf("SEE_REPLACEMENT passed case=user-overrides-generated\n");

    /* Truncated user.png must keep the last good replacement, not original. */
    {
        FILE* bad = fopen(user_path, "wb");
        if (!bad) {
            fprintf(stderr, "SEE_REPLACEMENT failed reason=truncate-open\n");
            see_destroy(see);
            return 1;
        }
        fputs("not a png", bad);
        fclose(bad);
        memcpy(frame, orig, sizeof(frame));
        see_present_rgb(see, frame, w, h);
        if (!all_rgb(frame, w, h, 16, 64, 200)) {
            fprintf(stderr, "SEE_REPLACEMENT failed reason=truncated-did-not-keep-last-good\n");
            see_destroy(see);
            return 1;
        }
        printf("SEE_REPLACEMENT passed case=truncated-keeps-last-good\n");
        /* Restore a valid user.png so later cases can still see the file. */
        if (see_png_write(user_path, user, w, h)) {
            fprintf(stderr, "SEE_REPLACEMENT failed reason=user-restore\n");
            see_destroy(see);
            return 1;
        }
    }

    /* reverted locks the asset: generated/user must not apply. */
    char reverted_path[512];
    snprintf(reverted_path, sizeof(reverted_path), "%s/TEST-000.00/%s/reverted", root, hash);
    FILE* rf = fopen(reverted_path, "wb");
    if (!rf) {
        fprintf(stderr, "SEE_REPLACEMENT failed reason=reverted-write\n");
        see_destroy(see);
        return 1;
    }
    fputs("reverted\n", rf);
    fclose(rf);
    memcpy(frame, orig, sizeof(frame));
    see_set_enabled(see, 1);
    see_present_rgb(see, frame, w, h);
    if (memcmp(frame, orig, sizeof(frame)) != 0) {
        fprintf(stderr, "SEE_REPLACEMENT failed reason=reverted-did-not-lock\n");
        see_destroy(see);
        return 1;
    }
    printf("SEE_REPLACEMENT passed case=reverted-locks\n");
    remove(reverted_path);
    remove(user_path);

    /* VRAM observer reads only. */
    uint16_t* vram = (uint16_t*)calloc(1024 * 512, sizeof(uint16_t));
    if (!vram) {
        see_destroy(see);
        return 1;
    }
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            vram[x + y * 1024] = (uint16_t)(x + y * 32);
    uint16_t* snapshot = (uint16_t*)malloc(1024 * 512 * sizeof(uint16_t));
    memcpy(snapshot, vram, 1024 * 512 * sizeof(uint16_t));
    see_on_vram_write(see, vram, 0, 0, 32, 32);
    if (memcmp(snapshot, vram, 1024 * 512 * sizeof(uint16_t)) != 0) {
        fprintf(stderr, "SEE_REPLACEMENT failed reason=vram-mutated\n");
        free(vram); free(snapshot); see_destroy(see);
        return 1;
    }
    free(vram);
    free(snapshot);
    printf("SEE_REPLACEMENT passed case=vram-unchanged\n");

    /* Texture-page dump on read, HD tagged by hash, enhance without replay. */
    {
        uint16_t* tvram = (uint16_t*)calloc(1024 * 512, sizeof(uint16_t));
        if (!tvram) {
            see_destroy(see);
            return 1;
        }
        for (int i = 0; i < 16; i++)
            tvram[i] = (uint16_t)((i * 2) | ((i * 3) << 5) | ((i * 5) << 10));
        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 64; x++) {
                int t0 = (x + y) & 0xf;
                int t1 = (x * 3 + y) & 0xf;
                int t2 = (x + y * 2) & 0xf;
                int t3 = (x * 5 + y * 3) & 0xf;
                tvram[x + y * 1024] = (uint16_t)(t0 | (t1 << 4) | (t2 << 8) | (t3 << 12));
            }
        }
        uint16_t* snap = (uint16_t*)malloc(1024 * 512 * sizeof(uint16_t));
        memcpy(snap, tvram, 1024 * 512 * sizeof(uint16_t));
        see_set_serial(see, "TEST-TEX.00");
        {
            char wipe[512];
            snprintf(wipe, sizeof(wipe), "%s/TEST-TEX.00", root);
            DIR* wd = opendir(wipe);
            if (wd) {
                struct dirent* e;
                char file[512];
                snprintf(file, sizeof(file), "%s/dumps.jsonl", wipe);
                remove(file);
                while ((e = readdir(wd))) {
                    if (e->d_name[0] == '.') continue;
                    char sub[512];
                    snprintf(sub, sizeof(sub), "%s/%s", wipe, e->d_name);
                    const char* names[] = { "orig.png", "generated.png", "user.png", "meta.json", "reverted", "edited", NULL };
                    for (int i = 0; names[i]; i++) {
                        snprintf(file, sizeof(file), "%s/%s", sub, names[i]);
                        remove(file);
                    }
                    rmdir(sub);
                }
                closedir(wd);
                rmdir(wipe);
            }
        }
        int before = see_asset_count(see);
        see_set_enabled(see, 0);
        see_on_texture_use(see, tvram, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        if (memcmp(snap, tvram, 1024 * 512 * sizeof(uint16_t)) != 0) {
            fprintf(stderr, "SEE_REPLACEMENT failed reason=texpage-vram-mutated\n");
            free(tvram); free(snap); see_destroy(see);
            return 1;
        }
        if (see_asset_count(see) != before + 1) {
            fprintf(stderr, "SEE_REPLACEMENT failed reason=texpage-not-dumped\n");
            free(tvram); free(snap); see_destroy(see);
            return 1;
        }
        see_on_texture_use(see, tvram, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        if (see_asset_count(see) != before + 1) {
            fprintf(stderr, "SEE_REPLACEMENT failed reason=texpage-redump\n");
            free(tvram); free(snap); see_destroy(see);
            return 1;
        }
        int made = see_enhance_cache(see);
        if (made < 1) {
            fprintf(stderr, "SEE_REPLACEMENT failed reason=enhance-cache made=%d\n", made);
            free(tvram); free(snap); see_destroy(see);
            return 1;
        }
        see_set_enabled(see, 1);
        see_on_texture_use(see, tvram, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        uint16_t orig_tex = tvram[1]; /* CLUT index 1, non-black */
        uint16_t replaced = see_replace_texel(see, 1, 0, 0, 0, 0, 0, 0, orig_tex);
        if (!orig_tex) {
            fprintf(stderr, "SEE_REPLACEMENT failed reason=clut1-black\n");
            free(tvram); free(snap); see_destroy(see);
            return 1;
        }
        if (replaced == orig_tex) {
            fprintf(stderr, "SEE_REPLACEMENT failed reason=hd-texel-unchanged\n");
            free(tvram); free(snap); see_destroy(see);
            return 1;
        }
        printf("SEE_REPLACEMENT passed case=texpage-dump-and-hd\n");
        made = see_enhance_cache(see);
        if (made != 0) {
            fprintf(stderr, "SEE_REPLACEMENT failed reason=enhance-cache-rerun made=%d\n", made);
            free(tvram); free(snap); see_destroy(see);
            return 1;
        }
        printf("SEE_REPLACEMENT passed case=enhance-cache-no-replay\n");
        free(tvram);
        free(snap);
    }

    see_destroy(see);
    puts("SEE_REPLACEMENT all cases passed");
    return 0;
}
