---
date: 2026-08-20 17:21
type: worklog
status: complete
tags: [qa, bios, discs, determinism, milestone-0]
---

# First real-game boots and golden hashes

## Goal

Media arrived (BIOS zip + two discs). Functionally verify the QA harness
against both titles and lock hashes that a later session can regress
against. Milestone 1 (Vulkan) is next, but this was the blocker.

## Done

- Extracted SCPH-1001 from `bios/PSX.zip`. SHA-256
  `71af94d1e47a68c11e8fdb9f8368040601514a42a5a399cda48c7d3bff1e99d3`.
- Extracted both 7z rips under `roms/` (BIN/CUE; Musashi is 4-track).
- Built `build/tools/armsx-qa` **without cmake/libchdr** — this machine
  has gcc but no cmake and no SDL2. PNG capture uses vendored
  `third_party/libchdr/deps/miniz-3.1.1/miniz.c`.
- **Legend of Legaia** (`SCUS-942.54`) booted to the title screen
  (New Game / Continue) and then into attract FMV.
- **Brave Fencer Musashi** (`SLUS-007.26`) booted to PUSH START BUTTON
  and then into a 3D attract demo.
- Hashes matched on a second independent run of each game. BIOS frames
  60–300 are identical across both titles.
- `make qa` no longer depends on cmake. `make test-boot` /
  `tests/boot_local.py` checks `tests/qa/goldens.json` and skips when
  media is missing.

Changed files:

- `Makefile` — QA links miniz.c directly; added `test-boot`.
- `tools/armsx_qa.c` — usage no longer claims CHD/ZIP for the harness.
- `tests/boot_local.py`, `tests/qa/*.qa`, `tests/qa/goldens.json`.
- `CLAUDE.md` — verification assets are present; `make test-boot`;
  `--stuck-frames` warning.
- Research: `docs/research/20260820_1719_qa-functional-boot.md`.
  The "NOT verified" block in `20260820_1645_ai-qa-harness.md` is
  marked superseded.

## Verified

Everything below was executed; output is real.

- `make qa && make test-qa` → selftest PNG
  `256x128 hash=8b5edfcb0984ad19`, `validate_qa.py` passed.
- `python3 tests/run_validation.py --case source --case cpu --case audio --case qa`
  → **exit 0**, 7.94s.
- `python3 tests/boot_local.py` → **exit 0**, both games:

```
ARMSX_BOOT passed id=SCUS-942.54 frame=240 640x480 hash=de6e29fed86fac80
ARMSX_BOOT passed id=SCUS-942.54 frame=840 320x228 hash=1c859cc02ebe806e
ARMSX_BOOT passed id=SLUS-007.26 frame=240 640x480 hash=de6e29fed86fac80
ARMSX_BOOT passed id=SLUS-007.26 frame=960 640x480 hash=ba1d94ecce2c4ac2
ARMSX_BOOT all present games passed ran=2
```

Title hashes confirmed on two runs each. Captures inspected: Sony logo,
PROKION logo, Legaia title, Musashi title, Legaia FMV, Musashi 3D demo.

## Broken / Known issues

- **Full gate cannot run on this machine.** No cmake, no `sdl2-config`.
  `test-gpu` (SDL.h), `test-chd`, `test-zip`, `test-sdl-audio`, and
  `./build.sh` all need those. Not a code regression — the previous
  session's green full gate was on a box that had them.
- QA harness no longer links libchdr, so **CHD discs will not open in
  armsx-qa** until cmake is back. Both local games are BIN/CUE.
- `bin/armsx` headed run: **NOT VERIFIED** (no SDL2).
- `--stuck-frames=300` would false-fail a sitting title screen. Do not
  use it that way.

## Open questions

1. cmake + libsdl2-dev + vulkan headers on this box? Milestone 1 cannot
   compile without them.
2. AI service and per-session budget cap (milestone 7) — unchanged.

## Next

**Milestone 1 — Vulkan backend skeleton.** The parity gate can now be a
real-game hash (`SCUS-942.54` frame 840 / `SLUS-007.26` frame 960) run
against software vs Vulkan, not just the synthetic triangles in
`tests/gpu_renderer_parity.c`. Install cmake, SDL2, and Vulkan headers,
then port `reference/swanstation/src/common/vulkan/*` behind a renderer
backend interface. Rename `USE_HARDWARE` in the same change.
