#include "enhance/see.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int walk_has_orig(const char* dir) {
    DIR* d = opendir(dir);
    if (!d) return 0;
    int found = 0;
    struct dirent* ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(path, &st)) continue;
        if (S_ISDIR(st.st_mode)) {
            found |= walk_has_orig(path);
        } else if (!strcmp(ent->d_name, "orig.png")) {
            found = 1;
        }
    }
    closedir(d);
    return found;
}

static int walk_has_name(const char* dir, const char* name) {
    DIR* d = opendir(dir);
    if (!d) return 0;
    int found = 0;
    struct dirent* ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(path, &st)) continue;
        if (S_ISDIR(st.st_mode)) found |= walk_has_name(path, name);
        else if (!strcmp(ent->d_name, name)) found = 1;
    }
    closedir(d);
    return found;
}

int main(void) {
    const char* cache = "build/tests/see-work/pack-cache";
    const char* dest = "build/tests/see-work/pack-out";
    mkdir("build", 0755);
    mkdir("build/tests", 0755);
    mkdir("build/tests/see-work", 0755);
    mkdir(cache, 0755);

    see_engine_t* see = see_create();
    if (!see) {
        fprintf(stderr, "SEE_PACK failed reason=alloc\n");
        return 1;
    }
    see_set_cache_root(see, cache);
    see_set_serial(see, "SCUS-942.54");
    see_set_enabled(see, 1);

    const int w = 24, h = 24;
    uint8_t rgb[24 * 24 * 3];
    for (int i = 0; i < w * h; i++) {
        rgb[i * 3 + 0] = (uint8_t)(i * 3);
        rgb[i * 3 + 1] = (uint8_t)(i * 5);
        rgb[i * 3 + 2] = (uint8_t)(i * 7);
    }
    char hash[17];
    if (see_ingest_rgb(see, rgb, w, h, hash)) {
        fprintf(stderr, "SEE_PACK failed reason=ingest\n");
        see_destroy(see);
        return 1;
    }
    char orig_path[1024];
    snprintf(orig_path, sizeof(orig_path), "%s/SCUS-942.54/%s/orig.png", cache, hash);
    struct stat st;
    if (stat(orig_path, &st)) {
        fprintf(stderr, "SEE_PACK failed reason=cache-missing-orig\n");
        see_destroy(see);
        return 1;
    }

    if (see_export_pack(see, dest)) {
        fprintf(stderr, "SEE_PACK failed reason=export\n");
        see_destroy(see);
        return 1;
    }

    if (walk_has_orig(dest)) {
        fprintf(stderr, "SEE_PACK failed reason=orig-png-in-pack\n");
        see_destroy(see);
        return 1;
    }
    if (!walk_has_name(dest, "generated.png")) {
        fprintf(stderr, "SEE_PACK failed reason=generated-missing\n");
        see_destroy(see);
        return 1;
    }
    if (!walk_has_name(dest, "manifest.json")) {
        fprintf(stderr, "SEE_PACK failed reason=manifest-missing\n");
        see_destroy(see);
        return 1;
    }

    char man_path[1024];
    snprintf(man_path, sizeof(man_path), "%s/manifest.json", dest);
    FILE* mf = fopen(man_path, "rb");
    if (!mf) {
        fprintf(stderr, "SEE_PACK failed reason=manifest-open\n");
        see_destroy(see);
        return 1;
    }
    char man[4096];
    size_t n = fread(man, 1, sizeof(man) - 1, mf);
    fclose(mf);
    man[n] = 0;
    if (!strstr(man, "SCUS-942.54") || !strstr(man, hash) || strstr(man, "orig.png")) {
        fprintf(stderr, "SEE_PACK failed reason=manifest-contents\n%s\n", man);
        see_destroy(see);
        return 1;
    }

    printf("SEE_PACK passed serial=SCUS-942.54 hash=%s stripped=orig.png\n", hash);
    puts("SEE_PACK all cases passed");
    see_destroy(see);
    return 0;
}
