#ifndef ARMSX_PLATFORM_FILE_H
#define ARMSX_PLATFORM_FILE_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef FILE* (*psxe_platform_fopen_callback_t)(const char* path, const char* mode, void* userdata);

void psxe_platform_set_fopen_callback(psxe_platform_fopen_callback_t callback, void* userdata);
FILE* psxe_platform_fopen(const char* path, const char* mode);

#ifdef __cplusplus
}
#endif

#endif
