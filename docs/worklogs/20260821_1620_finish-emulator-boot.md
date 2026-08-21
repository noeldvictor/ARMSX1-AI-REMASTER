---
date: 2026-08-21 16:20
type: worklog
status: complete
tags: [qa, gpu, milestone-0]
---

# Finish the emulator: golden boot of a real disc

## Goal
Prove the shipped headless QA path boots a local title to the stored
Sony-logo and title-screen hashes, twice, and that the default gate
exits 0.

## Done
- Confirmed local media: `bios/PSX - SCPH1001.BIN` (512 KiB) and
  `roms/Legend of Legaia (USA)/Legend of Legaia (USA).cue` (SCUS-942.54).
- Two consecutive `python3 tests/boot_local.py --game SCUS-942.54` runs
  matched `tests/qa/goldens.json` and each other. Play path was not
  changed.
- First full `python3 tests/run_validation.py` failed `chd` and `zip`
  (`cmake: not found`). Skip logic treated gitignored
  `third_party/cmake/bin/cmake` as present, but the Makefile called
  PATH `cmake`. Makefile now prefers PATH cmake and falls back to the
  vendored binary (`Makefile:20-28`, `Makefile:81`, `Makefile:424`).
  Source invariant in `tests/validate_sources.py`.
- Software GP0 path is still `gpu_render_triangle` in
  `frontend/gpu_hw.c:65`. SEE `vram-unchanged` still passes.
- Headed `./bin/armsx` was not launched: no `sdl2-config`.

## Verified
Executed; output is real.

Boot 1 and boot 2 (identical):

```
ARMSX_BOOT begin id=SCUS-942.54 frames=841 enhance=off
ARMSX_BOOT passed id=SCUS-942.54 frame=240 640x480 hash=de6e29fed86fac80
ARMSX_BOOT passed id=SCUS-942.54 frame=840 320x228 hash=1c859cc02ebe806e
ARMSX_BOOT all present games passed ran=1
```

Sony logo `de6e29fed86fac80` and title `1c859cc02ebe806e` are the
goldens. Both runs exit 0.

```
python3 tests/run_validation.py
→ exit 0, 124.863s
source, cpu, audio, qa, see, vk, boot, chd, zip passed
gpu, sdl-audio skipped reason=host-missing-sdl2
SEE_REPLACEMENT passed case=vram-unchanged
ARMSX_BOOT_SEE passed id=SCUS-942.54 off=1c859cc02ebe806e on=2f51fa06d28bb700 runs=2+2
```

`which sdl2-config` → not found.

## Broken / Known issues
- No headed SDL frontend on this host.
- `psx_save_state` / `psx_take_screenshot` / `psx_hard_reset` still
  `exit(1)`. Out of scope.
- Remaster milestones 1–9 (Vulkan rasterization, PGXP, HD textures,
  AI) are not this emulator-finish slice.

## Open questions
None for boot-to-title.

## Next
Headless Vulkan-vs-software triangle parity test (no SDL2), the
rasterizer gate before milestone 2.
