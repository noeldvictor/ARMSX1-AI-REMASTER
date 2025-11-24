#ifndef CHD_H
#define CHD_H

#include "disc.h"

// To-do: Implement CHD support

typedef struct {
    uint32_t dummy;
} chd_t;

chd_t* chd_create(void);
void chd_init(chd_t* cue);
int chd_load(chd_t* cue, const char* path);

// Disc interface
int chd_read(chd_t* cue, uint32_t lba, void* buf);
int chd_query(chd_t* cue, uint32_t lba);
int chd_get_track_number(chd_t* cue, uint32_t lba);
int chd_get_track_count(chd_t* cue);
int chd_get_track_lba(chd_t* cue, int track);
void chd_destroy(chd_t* cue);

#endif