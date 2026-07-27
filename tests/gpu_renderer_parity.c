#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../psx/dev/gpu.h"
#include "../frontend/gpu_hw.h"

void log_log(int level, const char* file, int line, const char* format, ...) {
    (void)level;
    (void)file;
    (void)line;
    (void)format;
}

void psx_ic_irq(psx_ic_t* ic, int id) {
    (void)ic;
    (void)id;
}

static psx_gpu_t* make_gpu(void) {
    psx_gpu_t* gpu = psx_gpu_create();
    if (!gpu)
        return NULL;
    psx_gpu_init(gpu, NULL);
    gpu->draw_x1 = 0;
    gpu->draw_y1 = 0;
    gpu->draw_x2 = PSX_GPU_FB_WIDTH - 1;
    gpu->draw_y2 = PSX_GPU_FB_HEIGHT - 1;
    return gpu;
}

static int run_case(const char* name, vertex_t a, vertex_t b, vertex_t c, poly_data_t data, int edge) {
    psx_gpu_t* reference = make_gpu();
    psx_gpu_t* accelerated = make_gpu();
    if (!reference || !accelerated) {
        fprintf(stderr, "GPU_PARITY failed case=%s reason=allocation\n", name);
        return 1;
    }

    for (size_t index = 0; index < PSX_GPU_VRAM_SIZE / sizeof(uint16_t); ++index) {
        const uint16_t value = (uint16_t)((index * 1103515245u + 12345u) >> 16);
        reference->vram[index] = value;
        accelerated->vram[index] = value;
    }

    gpu_render_triangle(reference, a, b, c, data, edge);
    gpu_hw_render_triangle(accelerated, a, b, c, data, edge);

    const int mismatch = memcmp(reference->vram, accelerated->vram, PSX_GPU_VRAM_SIZE) != 0;
    psx_gpu_destroy(reference);
    psx_gpu_destroy(accelerated);
    if (mismatch) {
        fprintf(stderr, "GPU_PARITY failed case=%s reason=vram-mismatch\n", name);
        return 1;
    }
    printf("GPU_PARITY passed case=%s\n", name);
    return 0;
}

int main(void) {
    int failed = 0;
    poly_data_t flat = {.attrib = 0};
    flat.v[0].c = flat.v[1].c = flat.v[2].c = 0x40a0f0;
    failed |= run_case(
        "flat",
        (vertex_t){.x = 10, .y = 12, .c = 0x40a0f0},
        (vertex_t){.x = 92, .y = 24, .c = 0x40a0f0},
        (vertex_t){.x = 38, .y = 105, .c = 0x40a0f0},
        flat,
        0
    );

    poly_data_t shaded = {.attrib = PA_SHADED};
    failed |= run_case(
        "shaded-dithered",
        (vertex_t){.x = 120, .y = 80, .c = 0x0000ff},
        (vertex_t){.x = 220, .y = 100, .c = 0x00ff00},
        (vertex_t){.x = 160, .y = 210, .c = 0xff0000},
        shaded,
        0
    );

    poly_data_t transparent = {.attrib = PA_TRANSP};
    transparent.v[0].c = transparent.v[1].c = transparent.v[2].c = 0xffffff;
    failed |= run_case(
        "semi-transparent",
        (vertex_t){.x = 300, .y = 40, .c = 0xffffff},
        (vertex_t){.x = 410, .y = 70, .c = 0xffffff},
        (vertex_t){.x = 340, .y = 170, .c = 0xffffff},
        transparent,
        1
    );

    if (failed)
        return 1;
    puts("GPU_PARITY all cases passed");
    return 0;
}
