#include <stdlib.h>

#include "chd.h"

chd_t* chd_create(void) {
    return (chd_t*)calloc(1, sizeof(chd_t));
}

void chd_init(chd_t* chd) {
    (void)chd;
}

int chd_load(chd_t* chd, const char* path) {
    (void)chd;
    (void)path;
    return 1;
}

int chd_read(chd_t* chd, uint32_t lba, void* buf) {
    (void)chd;
    (void)lba;
    (void)buf;
    return 0;
}

int chd_query(chd_t* chd, uint32_t lba) {
    (void)chd;
    (void)lba;
    return TS_FAR;
}

int chd_get_track_number(chd_t* chd, uint32_t lba) {
    (void)chd;
    (void)lba;
    return 0;
}

int chd_get_track_count(chd_t* chd) {
    (void)chd;
    return 0;
}

int chd_get_track_lba(chd_t* chd, int track) {
    (void)chd;
    (void)track;
    return 0;
}

void chd_destroy(chd_t* chd) {
    free(chd);
}
