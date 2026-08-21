#ifndef ARMSX_VK_BLIT_H
#define ARMSX_VK_BLIT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Headless Vulkan 1.1 buffer copy (no WSI). Uploads src, copies on the
 * device, reads dst. Returns 0 on byte-exact match, 1 on failure.
 * Used as the milestone-1 VRAM-blit skeleton; software VRAM stays the oracle.
 */
int vk_buffer_copy_roundtrip(const void* src, void* dst, size_t bytes);

/* Upload software-GPU VRAM (authoritative 16-bit framebuffer) through the
 * same device copy and read it back into out. out must be nbytes large.
 * Does not write *vram. */
int vk_copy_software_vram(const uint16_t* vram, uint16_t* out, size_t nbytes);

#ifdef __cplusplus
}
#endif

#endif
