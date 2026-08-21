#ifndef ARMSX_VK_RASTER_H
#define ARMSX_VK_RASTER_H

#include "psx/dev/gpu.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    VK_RASTER_OK = 0,
    VK_RASTER_FAIL = 1,
    /* Device cannot create a graphics pipeline / required UINT format. */
    VK_RASTER_NO_PIPELINE = 2
};

typedef struct {
    int draw_x1, draw_y1, draw_x2, draw_y2;
    int off_x, off_y;
    uint32_t gpustat;
} vk_raster_state_t;

/*
 * Rasterize one untextured triangle on a Vulkan graphics pipeline into a
 * 1024x512 BGR555 buffer. vram_in is the starting framebuffer and is not
 * written. Does not call gpu_render_triangle.
 */
int vk_raster_triangle(
    const uint16_t* vram_in,
    uint16_t* vram_out,
    vertex_t v0,
    vertex_t v1,
    vertex_t v2,
    poly_data_t data,
    const vk_raster_state_t* st
);

const char* vk_raster_last_step(void);
int vk_raster_last_vk(void);

#ifdef __cplusplus
}
#endif

#endif
