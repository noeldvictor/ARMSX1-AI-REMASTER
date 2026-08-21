---
date: 2026-08-21 16:09
type: worklog
status: complete
tags: [gpu, vulkan, milestone-1]
---

# Rename USE_HARDWARE to USE_GPU_BACKEND

## Goal
Stop labelling the software-rasterizer shim as a hardware renderer, which
the last session left as the next action.

## Done
- Mechanical rename `USE_HARDWARE` → `USE_GPU_BACKEND` in `Makefile`,
  `psx/dev/gpu.c`, `psx/dev/gpu.h`, `frontend/gpu_hw.c`, `frontend/gpu_hw.h`,
  `frontend/config.c`, `frontend/config.h`, `frontend/main.cpp` (50 sites).
- Makefile still defines it unconditionally (`Makefile:116-117`). Comment
  states it is a triangle-dispatch hook plus SDL presentation backend, not
  GPU rasterization.
- `psx/dev/gpu.h:86-89` documents the dispatch pointer: defaults to
  `gpu_render_triangle`, software stays authoritative.
- Source invariant in `tests/validate_sources.py:47-51` forbids the old
  token in the Makefile and GPU sources.
- `CLAUDE.md` updated. Historical research/worklogs that mention
  `USE_HARDWARE` were left as-is (append-only).
- Config still accepts `"hardware"` / `"hw"` as aliases for
  `sdl-accelerated` so old settings files keep working. Those tokens mean
  SDL presentation, not a rasterizer.

Carried from the previous session (still uncommitted): software VRAM
through `vk_copy_software_vram`.

## Verified
Executed; output is real.

```
python3 tests/run_validation.py --case source --case see --case vk --case gpu --case cpu --case qa
→ exit 0, 19.993s
source, see, vk, cpu, qa passed
gpu skipped reason=host-missing-sdl2
VK_VRAM_BLIT passed software-vram bytes=2097152 words=1048576
```

`gpu_renderer_parity.c` was **not** rebuilt (no `sdl2-config`). The
rename of `-DUSE_GPU_BACKEND` on that test rule is therefore
NOT VERIFIED by compile.

## Broken / Known issues
- `frontend/gpu_hw.c` and names like `hardware_backend_active_`,
  `hardwareRendererAvailable()`, `finishHardwareFrame()` still say
  "hardware". The compile-time lie is gone; the identifiers are not.
- Still no swapchain / SDL present.
- This is still present-path blit parity, not GP0 triangle rasterization
  on Vulkan.

## Open questions
None for this slice.

## Next
Add a headless Vulkan-vs-software *triangle* parity test (no SDL2) so the
rasterizer gate exists before milestone-2 rasterization. Do not hang it
on `tests/gpu_renderer_parity.c` — that file includes `gpu_hw.h` / SDL
and skips on this host.
