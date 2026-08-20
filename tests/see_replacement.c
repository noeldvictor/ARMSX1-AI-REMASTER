#include "enhance/see.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

    see_destroy(see);
    puts("SEE_REPLACEMENT all cases passed");
    return 0;
}
