---
date: 2026-08-20 21:39
type: worklog
status: complete
tags: [vulkan, see, milestone-1]
---

# Vulkan blit skeleton + last-good PNG reload

## Goal

Continue past the landed 2D vertical. Milestone 1 was blocked on cmake /
Vulkan headers; both are now vendored locally (gitignored). SDL2 is still
missing, so this is a headless buffer copy, not a swapchain.

## Done

- `vk/blit.c` — Vulkan 1.1 instance/device, host-visible buffers,
  `vkCmdCopyBuffer` round-trip. No WSI.
- `tests/vk_vram_blit.c` + `make test-vk`. Links `libvulkan.so.1` (no
  `libvulkan-dev` symlink on this box).
- `see_present_rgb` keeps the last good replacement when a PNG is truncated
  or malformed (ADR-004 hot-reload trap).
- Gate case `vk`; skips if headers are absent.
- `third_party/cmake/` and `third_party/vulkan-headers/` gitignored.

## Verified

Executed; output is real.

```
SEE_REPLACEMENT passed case=truncated-keeps-last-good
VK_VRAM_BLIT passed 64x64 bytes=8192
python3 tests/run_validation.py --case source --case see --case vk
→ exit 0, 0.129s
```

Mesa printed `Haswell Vulkan support is incomplete` then the copy matched.

## NOT verified

- Swapchain / headed present (no SDL2).
- Software-vs-Vulkan *rasterizer* parity. This is a buffer copy, not GP0
  triangles.
- `./build.sh` still needs SDL2.

## Next

Use this blit to upload software VRAM and read it back as the first
Vulkan-vs-software *present* parity check, still without a window. Then
rename `USE_HARDWARE`.
