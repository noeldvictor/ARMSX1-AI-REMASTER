#ifndef ARMSX_VK_BLIT_H
#define ARMSX_VK_BLIT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Headless Vulkan 1.1 buffer copy (no WSI). Uploads src, copies on the
 * device, reads dst. Returns 0 on byte-exact match, 1 on failure.
 * Used as the milestone-1 VRAM-blit skeleton; software VRAM stays the oracle.
 */
int vk_buffer_copy_roundtrip(const void* src, void* dst, size_t bytes);

#ifdef __cplusplus
}
#endif

#endif
