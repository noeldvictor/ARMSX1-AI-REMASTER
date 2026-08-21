---
date: 2026-08-20 22:03
type: worklog
status: complete
tags: [vulkan, gpu, milestone-1]
---

# Software VRAM through the Vulkan blit

## Goal
Copy the software GPU's authoritative VRAM through the existing headless
Vulkan buffer copy and prove byte-exact readback, still with no window.

## Done
- `vk_copy_software_vram` (`vk/blit.c`) wraps `vk_buffer_copy_roundtrip`
  and does not write the source VRAM.
- `tests/vk_vram_blit.c` now creates a software GPU, fills VRAM, draws a
  flat triangle with `gpu_render_triangle`, then copies the full 2 MiB
  framebuffer through Vulkan and `memcmp`s against `gpu->vram`.

## Verified
`make test-vk`:

```
VK_VRAM_BLIT passed software-vram bytes=2097152 words=1048576
VK_VRAM_BLIT all cases passed
```

`python3 tests/run_validation.py --case source --case see --case vk` →
exit 0, 0.246s. `see` and `vk` passed (not skipped). Mesa: Haswell
Vulkan support is incomplete; the copy still matched.

## Broken / Known issues
- Still no swapchain / SDL present.
- This is present-path blit parity, not GP0 triangle rasterization on
  Vulkan.

## Open questions
None for this slice.

## Next
Rename `USE_HARDWARE` so it no longer labels the software-rasterizer shim
as a hardware renderer.
