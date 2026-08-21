#include "vk/blit.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

int main(void) {
    /* 64x64 BGR555 VRAM rectangle — the milestone-1 blit unit. */
    enum { W = 64, H = 64, N = W * H };
    uint16_t src[N];
    uint16_t dst[N];
    for (int i = 0; i < N; i++)
        src[i] = (uint16_t)((i * 17u) ^ 0x55AAu);
    memset(dst, 0, sizeof(dst));

    if (vk_buffer_copy_roundtrip(src, dst, sizeof(src))) {
        fprintf(stderr, "VK_VRAM_BLIT failed reason=roundtrip\n");
        return 1;
    }
    if (memcmp(src, dst, sizeof(src)) != 0) {
        fprintf(stderr, "VK_VRAM_BLIT failed reason=mismatch\n");
        return 1;
    }
    printf("VK_VRAM_BLIT passed %dx%d bytes=%zu\n", W, H, sizeof(src));
    puts("VK_VRAM_BLIT all cases passed");
    return 0;
}
