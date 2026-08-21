#include "vk/blit.h"
#include "vk/raster.h"
#include "psx/dev/gpu.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

int main(void) {
    psx_gpu_t* gpu = make_gpu();
    if (!gpu || !gpu->vram) {
        fprintf(stderr, "VK_VRAM_BLIT failed reason=gpu-alloc\n");
        return 1;
    }

    const size_t nbytes = PSX_GPU_VRAM_SIZE;
    const size_t nwords = nbytes / sizeof(uint16_t);
    for (size_t i = 0; i < nwords; i++)
        gpu->vram[i] = (uint16_t)((i * 1103515245u + 12345u) >> 16);

    poly_data_t flat = {.attrib = 0};
    flat.v[0].c = flat.v[1].c = flat.v[2].c = 0x40a0f0;
    gpu_render_triangle(
        gpu,
        (vertex_t){.x = 10, .y = 12, .c = 0x40a0f0},
        (vertex_t){.x = 92, .y = 24, .c = 0x40a0f0},
        (vertex_t){.x = 38, .y = 105, .c = 0x40a0f0},
        flat,
        0
    );

    uint16_t* out = (uint16_t*)malloc(nbytes);
    if (!out) {
        psx_gpu_destroy(gpu);
        fprintf(stderr, "VK_VRAM_BLIT failed reason=out-alloc\n");
        return 1;
    }
    memset(out, 0, nbytes);

    if (vk_copy_software_vram(gpu->vram, out, nbytes)) {
        fprintf(stderr, "VK_VRAM_BLIT failed reason=roundtrip\n");
        free(out);
        psx_gpu_destroy(gpu);
        return 1;
    }
    if (memcmp(gpu->vram, out, nbytes) != 0) {
        fprintf(stderr, "VK_VRAM_BLIT failed reason=vram-mismatch bytes=%zu\n", nbytes);
        free(out);
        psx_gpu_destroy(gpu);
        return 1;
    }

    printf("VK_VRAM_BLIT passed software-vram bytes=%zu words=%zu\n", nbytes, nwords);

    /* Vulkan-vs-software triangle rasterization (not a VRAM blit). */
    {
        psx_gpu_t* sw = make_gpu();
        uint16_t* vk_fb = (uint16_t*)malloc(nbytes);
        if (!sw || !sw->vram || !vk_fb) {
            free(vk_fb);
            if (sw) psx_gpu_destroy(sw);
            free(out);
            psx_gpu_destroy(gpu);
            fprintf(stderr, "VK_TRIANGLE failed reason=alloc\n");
            return 1;
        }

        vk_raster_state_t st = {
            .draw_x1 = 0,
            .draw_y1 = 0,
            .draw_x2 = PSX_GPU_FB_WIDTH - 1,
            .draw_y2 = PSX_GPU_FB_HEIGHT - 1,
            .off_x = 0,
            .off_y = 0,
            .gpustat = sw->gpustat,
        };

        struct {
            const char* name;
            vertex_t a, b, c;
            poly_data_t data;
            int edge;
        } cases[3];

        cases[0].name = "flat";
        cases[0].a = (vertex_t){ .x = 10, .y = 12, .c = 0x40a0f0 };
        cases[0].b = (vertex_t){ .x = 92, .y = 24, .c = 0x40a0f0 };
        cases[0].c = (vertex_t){ .x = 38, .y = 105, .c = 0x40a0f0 };
        cases[0].data = (poly_data_t){ .attrib = 0 };
        cases[0].data.v[0].c = cases[0].data.v[1].c = cases[0].data.v[2].c = 0x40a0f0;
        cases[0].edge = 0;

        cases[1].name = "shaded-dithered";
        cases[1].a = (vertex_t){ .x = 120, .y = 80, .c = 0x0000ff };
        cases[1].b = (vertex_t){ .x = 220, .y = 100, .c = 0x00ff00 };
        cases[1].c = (vertex_t){ .x = 160, .y = 210, .c = 0xff0000 };
        cases[1].data = (poly_data_t){ .attrib = PA_SHADED };
        cases[1].edge = 0;

        cases[2].name = "semi-transparent";
        cases[2].a = (vertex_t){ .x = 300, .y = 40, .c = 0xffffff };
        cases[2].b = (vertex_t){ .x = 410, .y = 70, .c = 0xffffff };
        cases[2].c = (vertex_t){ .x = 340, .y = 170, .c = 0xffffff };
        cases[2].data = (poly_data_t){ .attrib = PA_TRANSP };
        cases[2].data.v[0].c = cases[2].data.v[1].c = cases[2].data.v[2].c = 0xffffff;
        cases[2].edge = 1;

        int skipped = 0;
        for (int ci = 0; ci < 3; ci++) {
            for (size_t i = 0; i < nwords; i++) {
                uint16_t v = (uint16_t)((i * 1103515245u + 12345u) >> 16);
                sw->vram[i] = v;
                vk_fb[i] = v;
            }
            gpu_render_triangle(sw, cases[ci].a, cases[ci].b, cases[ci].c, cases[ci].data, cases[ci].edge);
            int rr = vk_raster_triangle(
                vk_fb, vk_fb, cases[ci].a, cases[ci].b, cases[ci].c, cases[ci].data, &st
            );
            if (rr == VK_RASTER_NO_PIPELINE) {
                printf(
                    "VK_TRIANGLE skip reason=pipeline step=%s vk=%d\n",
                    vk_raster_last_step(),
                    vk_raster_last_vk()
                );
                skipped = 1;
                break;
            }
            if (rr != VK_RASTER_OK) {
                fprintf(
                    stderr,
                    "VK_TRIANGLE failed case=%s reason=raster step=%s vk=%d\n",
                    cases[ci].name,
                    vk_raster_last_step(),
                    vk_raster_last_vk()
                );
                free(vk_fb);
                psx_gpu_destroy(sw);
                free(out);
                psx_gpu_destroy(gpu);
                return 1;
            }
            if (memcmp(sw->vram, vk_fb, nbytes) != 0) {
                fprintf(stderr, "VK_TRIANGLE failed case=%s reason=vram-mismatch\n", cases[ci].name);
                free(vk_fb);
                psx_gpu_destroy(sw);
                free(out);
                psx_gpu_destroy(gpu);
                return 1;
            }
            printf("VK_TRIANGLE passed case=%s\n", cases[ci].name);
        }
        free(vk_fb);
        psx_gpu_destroy(sw);
        if (!skipped)
            puts("VK_TRIANGLE all cases passed");
    }

    puts("VK_VRAM_BLIT all cases passed");
    free(out);
    psx_gpu_destroy(gpu);
    return 0;
}
