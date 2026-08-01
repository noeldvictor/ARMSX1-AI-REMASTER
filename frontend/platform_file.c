#include "platform_file.h"

static psxe_platform_fopen_callback_t g_fopen_callback;
static void* g_fopen_userdata;

void psxe_platform_set_fopen_callback(psxe_platform_fopen_callback_t callback, void* userdata) {
    g_fopen_callback = callback;
    g_fopen_userdata = userdata;
}

FILE* psxe_platform_fopen(const char* path, const char* mode) {
    if (g_fopen_callback) {
        FILE* file = g_fopen_callback(path, mode, g_fopen_userdata);
        if (file || (path && path[0])) {
            return file;
        }
    }

    return fopen(path, mode);
}
