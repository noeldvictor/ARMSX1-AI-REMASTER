---
date: 2026-08-20 14:38
type: worklog
status: complete
tags: [bootstrap, ci, docs, build-fix, milestone-0]
---

# Milestone 0 — repo bootstrap, work tracking, CI removal

## Goal

Turn the fork into something an autonomous AI agent can work in: establish the
tracking system, remove cloud CI, measure the baseline against DuckStation, and
record the architecture decisions.

## Done

### CI removed

- `.gitea/workflows/build.yml` → `docs/archive/ci/gitea-build.yml` (kept as a
  reference for platform build flags; nothing runs).
- `.github/FUNDING.yml` deleted — pointed donations at the upstream `psxe`
  author's Buy Me a Coffee, which a fork should not do.
- `.gitea/` and `.github/` directories removed entirely.
- **Finding:** there were never any `.github/workflows/` files. GitHub only
  reads `.github/workflows/`, so the Gitea matrix was already inert here and no
  Actions minutes were ever being billed. Rationale written up in
  `docs/archive/ci/README.md`.

### Documentation and tracking established

- `docs/README.md` — naming convention (`YYYYMMDD_HHMM_slug.md`), directory map,
  worklog template, append-only record rule, honesty rule.
- `docs/research/20260820_1438_duckstation-feature-gap.md` — source-verified gap
  analysis, every claim cited to a line.
- `docs/decisions/20260820_1450_remaster-architecture.md` — ADR-001, all six
  architecture decisions with rejected alternatives.
- `CLAUDE.md` — agent operating manual: non-negotiables, known traps, protocol.
- `README.md` — rewritten for the fork: no-support notice, upstream comparison,
  honest status.

### Two pre-existing build bugs fixed

Both were blocking the validation gate on a clean checkout. Neither was caused
by this session's changes.

1. **`psx/dev/mcd.c` — `PATH_MAX` undeclared.** The main build sets no `-std=`
   flag, so `$(CC)` defaults to `gnu*` where glibc exposes `PATH_MAX`. The test
   rules force `-std=c11`, which is strict-ANSI and hides it behind feature-test
   macros. Result: the app built, the `cpu` test did not. Added a guarded
   `#ifndef PATH_MAX` fallback.

2. **`Makefile:133` — miniz deflate APIs disabled.** libchdr's vendored miniz
   defaults `MINIZ_DEFLATE_APIS` to `OFF`
   (`third_party/libchdr/deps/miniz-3.1.1/CMakeLists.txt:2`) because libchdr only
   needs inflate. `frontend/archive.cpp` needs deflate, so the `zip` test failed
   to link with nine undefined `mz_deflate*` / `mz_compress*` symbols. Added
   `-DMINIZ_DEFLATE_APIS=ON` to `LIBCHDR_CMAKE_ARGS`.

## Verified

Both commands were run. Output is real, not assumed.

**Gate — `python3 tests/run_validation.py`, exit 0:**

```
ARMSX_VALIDATION passed case=source     elapsed=0.032s
ARMSX_VALIDATION passed case=cpu        elapsed=0.065s
ARMSX_VALIDATION passed case=gpu        elapsed=0.032s
ARMSX_VALIDATION passed case=audio      elapsed=0.032s
ARMSX_VALIDATION passed case=sdl-audio  elapsed=0.114s
ARMSX_VALIDATION passed case=chd        elapsed=6.978s
ARMSX_VALIDATION passed case=zip        elapsed=1.567s
ARMSX_VALIDATION passed case=web        elapsed=0.164s
ARMSX_VALIDATION all selected cases passed elapsed=8.985s
```

Before this session the same command gave
`failures=cpu:exit-2,chd:exit-2,zip:exit-2,web:exit-1`.

**Build — `./build.sh`, exit 0:** produced `bin/armsx`, 12,472,544 bytes.
Warnings only (`framebuffer_scale` and `session_steps` set-but-unused in
`frontend/main.cpp`, one unused static function). Not addressed — pre-existing
and harmless.

**GPU parity oracle confirmed working** — `gpu_renderer_parity` passes `flat`,
`shaded-dithered`, and `semi-transparent`. This is the harness the Vulkan
renderer will be validated against in milestone 1.

## Broken / Known issues

- **`bin/armsx` was never launched.** It links and the binary exists, but no
  game was booted, so **runtime behaviour is NOT VERIFIED.** No BIOS or disc
  images are available (`bios/`, `roms/`, `*.bin`, `*.cue` are all gitignored).
- **`frontend/config.c:315` has the same latent `PATH_MAX` bug** as `mcd.c`. It
  does not currently break anything because nothing compiles it under
  `-std=c11`. Left alone deliberately — noted here so a future session
  recognises it rather than rediscovering it.
- `third_party/SDL` submodule remains uninitialized. Not needed; the build uses
  system SDL2.

## Discovered

**Submodules are required and the README never said so.** A clean checkout
fails to build. Required:

```sh
git submodule update --init --depth 1 third_party/libchdr third_party/fuse-lib
git -C third_party/fuse-lib submodule update --init --recursive --depth 1
```

fuse-lib has its own nested submodules (`imgui`, `cmrc`, `glad`); without the
second command CMake fails with `add_subdirectory given source
"third_party/glad/cmake" which is not an existing directory`. Note `imgui` is
pulled from `git.nanodata.cloud`, not GitHub — a third-party availability
dependency worth knowing about.

## Open questions

1. **BIOS and disc image paths.** Blocks the visual half of the verification
   loop and therefore all of milestones 3–7. This is the top blocker.
2. **AI service credentials and budget cap.** Which provider for texture
   upscaling, and what hard per-session spend limit should the code enforce?
   Needed by milestone 7, not before.
3. Does an Android NDK 27+ install exist on this machine? Needed to keep the
   Adreno target honest rather than theoretical.

## Next

**Milestone 1: Vulkan backend skeleton.** Specifically — define the renderer
backend interface, stand up instance/device/swapchain selection with a Vulkan
1.1 baseline and 1.3 capability probing, and get a VRAM blit presenting with
byte-exact parity against the software path. Extend
`tests/gpu_renderer_parity.c` with the Vulkan-vs-software comparison before
writing the renderer, so the gate exists before the thing it gates.

Rename `USE_HARDWARE` at the same time — it currently guards a shim that
forwards to the software rasterizer (`frontend/gpu_hw.c:64`) and will become
actively dangerous once a real hardware path exists.
