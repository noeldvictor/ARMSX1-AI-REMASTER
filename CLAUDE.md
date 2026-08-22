# CLAUDE.md

Operating manual for AI agents working in this repository. Read this before
touching anything.

## What this repo is

`ARMSX1-AI-REMASTER` is a personal, experimental fork of
[ARMSX1](https://github.com/momo-AUX1/ARMSX1) (itself a fork of `psxe`). It is a
PlayStation 1 emulator being rebuilt to **auto-remaster games while you play
them** — HD textures, corrected geometry, and possibly replaced meshes, produced
algorithmically and by AI APIs at runtime.

Development is intentionally AI-driven. That is the point of the project, not an
implementation detail.

**This is not a supported emulator.** Do not optimise for users, releases,
backwards compatibility with upstream, or contributor onboarding. There are none.

## ALL WORK MUST BE LOGGED

This is not optional and it is not a formality. It is the only mechanism by
which one agent session can learn what another did.

- **Research** → `docs/research/YYYYMMDD_HHMM_slug.md`
- **Decisions** → `docs/decisions/YYYYMMDD_HHMM_slug.md`
- **Session log** → `docs/worklogs/YYYYMMDD_HHMM_slug.md`

Get the timestamp with `date +"%Y%m%d_%H%M"`. Never invent one.

**Every session ends by writing a worklog.** No exceptions, including sessions
that produced no code — "investigated X, it was a dead end, here is why" is one
of the most valuable things you can leave behind.

**Anything you learn that is not obvious from the code is a research doc**, not
a code comment and not a line in a commit message.

The record is **append-only**. If a document turns out to be wrong, write a new
one and add `Superseded by:` to the old one. Never rewrite history — a wrong
conclusion honestly reached and later corrected is more useful than a tidy lie.

Full conventions and the worklog template: [`docs/README.md`](docs/README.md).

## The goal, stated precisely

A **Super Enhancement Engine for PlayStation** (ADR-003) — per-game,
individually toggleable enhancement layers over an accurate emulator:

1. **Corrected geometry** — no polygon wobble, no swimming textures (PGXP).
2. **HD textures** — upscaled algorithmically, then AI-enhanced, cached on first
   sighting.
3. **Normal maps + per-pixel lighting** — PS1 surfaces have none. Highest
   value-to-risk ratio on the roadmap; prioritise over mesh work.
4. **Height / parallax mapping** — apparent geometric detail with no mesh
   identification problem.
5. **Audio** — SPU ADPCM sample replacement.
6. **Mesh replacement** — research only, may fail.

All appearing *during play*, with no manual asset preparation step.

### The central hypothesis — and the honest risk

The model is [SUPER ZSNES](https://www.zsnes.com/), which ships this for the
SNES. Their own docs say their hi-res mode is **"not just an auto upscalar"** —
they built a manual drawing tool because auto-upscaling was not good enough, and
have ~10 games after about a year of work by the original ZSNES authors.

**This project's bet is that AI closes the gap between automatic and hand-drawn
quality.** It may fail. If it does, the fallback is their model — a curation
tool plus a per-game pack format — which this architecture produces anyway.

Do not write documentation or worklogs that assume the bet has been won. PS1 is
*harder* than SNES here: 3D geometry rather than tile layers, no fixed tile
grid, textures in VRAM pages sampled through CLUTs rather than as addressable
assets. **One convincingly enhanced game is a real result.**

**The risk is now hedged** (ADR-004). AI does a first pass; the human touches up
in their own image editor with hot reload. At 70% AI quality this still wins; at
20% it degrades into SUPER ZSNES's model with a draft instead of a blank canvas.
There is no quality level at which the project produces nothing.

### Human-in-the-loop pipeline (ADR-004)

- **We do not build an image editor.** Assets are PNGs; the user edits them in
  Photoshop/Krita/Aseprite and we **hot-reload on save**. Our surface is a file
  watcher, a review queue, and an A/B hotkey.
- **Auto-apply everything.** AI output goes live immediately with no approval
  gate. Garbled fonts and logos *will* appear in-game until fixed — accepted
  trade. The review queue is a **cleanup** queue, not a gate.
- **Resolution order at draw time:** `user.png` → `ai.png` → original VRAM.
- **`reverted` and `edited` LOCK an asset** against further AI passes. Without
  this, auto-apply re-enhances rejected assets forever. This is load-bearing.

### Pack format constraint (day one, not retrofitted)

Enhancement packs carry **hashes plus generated assets only — never PS1-derived
imagery or audio.** This is what keeps packs legally shareable.

**The local cache keeps `orig.png` (the editor needs it as reference); the pack
export step deletes it.** Getting this backwards makes packs unshippable. It is
a format constraint from day one, not a later filter.

### Hot reload traps

- **Debounce the watcher.** Image editors write in several syscalls; a naive
  watcher fires mid-write and loads a truncated PNG.
- A malformed or partial PNG must **keep the previous texture** — never crash,
  never show garbage.
- Decode off-thread; swap on the frame boundary. Never stall the render thread.

## Non-negotiables

These exist because violating them destroys work that cannot be recovered.

1. **The software rasterizer is the parity oracle. Never delete it, never
   "replace" it.** `psx/dev/gpu.c` encodes years of game-specific fixes — see
   `compat.txt` and comments like *"Fixes Mortal Kombat II, Bubble Bobble,
   Driver 1 & 2"* (`psx/dev/gpu.c:1109`). The Vulkan renderer is validated
   *against* it. If Vulkan and software disagree, **software is right** until
   proven otherwise on hardware.

2. **Accuracy is not traded for looks.** Enhancement is a presentation layer.
   Emulated GPU results must not change when enhancements are toggled. If a
   remaster feature requires changing emulation, it needs an explicit toggle
   defaulting to off.

3. **Every session writes a worklog.** No exceptions. See `docs/README.md`.

4. **Never claim something works without running it.** If you did not execute
   it, write "NOT VERIFIED". Optimistic worklogs poison every future session,
   because future sessions cannot re-run the past.

5. **No cloud CI. Ever.** It was deliberately removed
   (`docs/archive/ci/README.md`). Do not add `.github/workflows/`. Verification
   is local.

## Architecture you must know before editing the GPU

Read [`docs/research/20260820_1438_duckstation-feature-gap.md`](docs/research/20260820_1438_duckstation-feature-gap.md)
in full before your first GPU change. The essentials:

- **`frontend/gpu_hw.c` is not a hardware renderer.** It is a shim that forwards
  to the software rasterizer (`gpu_hw.c:64`). `USE_GPU_BACKEND` is defined
  unconditionally in the Makefile. It is a triangle-dispatch hook plus SDL
  presentation selection, not GPU-accelerated rasterization. The old name
  `USE_HARDWARE` was a lie and must not return.
- **"SDL accelerated" is a presentation mode**, not a renderer. It uploads dirty
  VRAM scanlines to an SDL texture. It does not rasterize.
- **Vertices reaching the rasterizer are integer 2D screen coordinates** —
  `int16_t x, y` with `uint8_t tx, ty` (`psx/dev/gpu.h:73-77`). No Z, no W. This
  is why PS1 games wobble and why textures swim, and it cannot be fixed
  downstream.
- **The GTE is complete** (`psx/cpu.c:92-110`). It is the PGXP tap point, and it
  already works. Do not rewrite it.
- **VRAM is authoritative**: 1024×512 × 16-bit, `psx/dev/gpu.h:17-21`.

## Decisions already made — do not relitigate

Full reasoning in [`docs/decisions/20260820_1450_remaster-architecture.md`](docs/decisions/20260820_1450_remaster-architecture.md).

| | Decision |
| --- | --- |
| Render path | Full DuckStation-class rewrite: Vulkan + PGXP + internal scaling + texture replacement |
| Graphics API | **Vulkan only.** Baseline 1.1 core, 1.3 opportunistic behind capability checks |
| Platforms | **Desktop Linux + Android only.** Reference device: AYN Thor (Snapdragon 8 Gen 2 / Adreno 740; Lite = SD865 / Adreno 650) |
| Removed ports | `uwp/`, `psvita/`, `web/`, `ios/` **deleted** 2026-08-20. Recoverable from git history only |
| AI timing | Live async with progressive disk cache. Algorithmic first, AI as an upgrade pass |
| 3D scope | Textures + PGXP ship. Mesh replacement (Tripo) is a research track that never blocks the others |
| Verification | Headless parity gate on every change; real discs for visual spot-checks |

If you believe a decision is wrong, **write a new ADR arguing it**. Do not
quietly build something else.

## Licensing — read before copying any code

**This project is GPL-3.0** (ADR-002, 2026-08-20). It was LGPL-3.0; LGPLv3 §2
permitted the change. Previous text kept at `LICENSES/PREVIOUS-LGPL3.txt`.

- **NEVER copy from `stenzek/duckstation`.** It is CC BY-NC-ND 4.0.
  NoDerivatives means what it says. This cannot be cured by deleting it later.
- **DO use `reference/swanstation`** — GPL-3.0, forked from DuckStation's last
  GPLv3 commit, actively maintained. Cloned locally, gitignored, pinned at
  `7f69c19`. Provenance recorded in `LICENSES/SWANSTATION-GPL3.txt`.
- **Verify per-file headers, not the repo badge.** GPLv2-**only** is
  incompatible with GPLv3. Already audited: SwanStation's `pgxp.cpp`/`.h` are
  GPLv2-**or-later** (iCatButler, 2016) and therefore fine; the `gpu_hw*` and
  `texture_replacements` files carry no per-file header so repo GPL-3.0 applies.
- **Preserve attribution headers** on every derived file — name SwanStation and
  the original authors. Keep iCatButler's PGXP notice intact.
- Distributing binaries obliges offering full corresponding source.

Port plan and sizes: [`docs/research/20260820_1600_swanstation-port-plan.md`](docs/research/20260820_1600_swanstation-port-plan.md).

### Porting rules

Porting is **reimplementation against a reference**, not a merge. SwanStation is
C++17 with a `GPUBackend` class hierarchy and a libretro host; ARMSX1 is C11 free
functions on `psx_gpu_t` with an SDL2 frontend. `gpu_hw_vulkan.cpp` will not
compile here.

Do **not** port: the dynarec (ADR-001 keeps the no-codegen property
deliberately), the libretro host interface, or the D3D11/D3D12/OpenGL backends.

## Milestones

Current position: **milestone 0 complete**. Next: milestone 1.

| # | Deliverable | Gate |
| --- | --- | --- |
| 0 | Repo bootstrap, tracking, CI removal | done |
| 1 | Vulkan backend skeleton, VRAM blit parity | Parity hashes |
| 2 | Vulkan rasterization, all primitive types | Parity hashes |
| 3 | Internal resolution scaling | Visual + parity at 1x |
| 3b | Per-game profile system (disc-serial keyed) | Unit |
| 4 | PGXP vertex pipeline + widescreen FOV | Wobble gone, no regressions |
| 4b | Save states | Round-trip determinism |
| 5 | Texture hashing, dumping, cache | Dumps match VRAM |
| 5b | Cache format, manifest, asset state machine | Round-trip |
| 6 | Replacement, 2D / VRAM-write | HD pack renders |
| 6b | Replacement, 3D texture pages — original work | Visual |
| 6c | Normal maps + per-pixel lighting | Visual |
| 6d | Height map / parallax mapping | Visual |
| 6e | Hotkey A/B compare (whole-frame original) | Visual |
| 7 | Live async AI enhancement worker | Budget cap honoured |
| 7b | CPU overclock | No timing regressions |
| 7c | File watcher + hot reload | Edit-to-screen < 1s |
| 7d | Review queue UI (orig / AI / user) | Manual |
| 7e | Pack export (strips orig.png, writes provenance) | No PS1 data in output |
| 8 | Audio: SPU ADPCM sample replacement | Visual/aural |
| 9 | Mesh fingerprinting research | Written up either way |

Full reasoning: [`docs/decisions/20260820_1620_super-enhancement-engine.md`](docs/decisions/20260820_1620_super-enhancement-engine.md).

### SUPER ZSNES is closed source

**Take no code from it and do not seek any.** It is a proprietary from-scratch
rewrite. Ideas and architecture are not copyrightable; implementations are.
Everything derived from it here is independently implemented. (This is separate
from SwanStation, which *is* GPL-3.0 and *is* a code source.)

## Working session protocol

**Start:**
1. Read the most recent file in `docs/worklogs/`.
2. Read any `docs/research/` doc relevant to what you are about to touch.
3. State which milestone you are working on.

**During:**
- Small, verifiable steps. Run the gate often.
- When you discover something non-obvious about the codebase, that is a
  `docs/research/` doc, not a code comment.

**End — mandatory:**
1. Write `docs/worklogs/YYYYMMDD_HHMM_slug.md` using the template in
   `docs/README.md`.
2. Run the gate and record the actual output.
3. State the single most useful next action.

## Driving a real game (AI QA harness)

`build/tools/armsx-qa` (via `make qa`) runs a game **headless and
deterministically** — no window, no audio device, no host input. Use it to
verify anything visual instead of guessing.

```sh
make qa

# boot both local titles and check golden hashes (~40s each)
make test-boot

# one game, capture the title screen
./build/tools/armsx-qa --bios="bios/PSX - SCPH1001.BIN" \
    --cdrom="roms/Legend of Legaia (USA)/Legend of Legaia (USA).cue" \
    --script=tests/qa/scus-942.54-title.qa --capture-dir=/tmp/caps --json

# same boot with the enhancement layer on (presentation only; VRAM unchanged)
./build/tools/armsx-qa --bios="bios/PSX - SCPH1001.BIN" \
    --cdrom="roms/Legend of Legaia (USA)/Legend of Legaia (USA).cue" \
    --script=tests/qa/scus-942.54-title.qa --enhance --enhance-dir=cache --json

# Texture dump + HD without replaying: every unique 2D surface and CLUT-decoded
# 256×256 texture page lands in cache/<serial>/<hash>/{orig.png,meta.json}.
# Drop user.png in that folder (or run see_enhance_cache) to tag the HD
# version. For translations, save xlat-en.png (or xlat-XX.png) next to orig.png
# and pass --lang=en. catalog.html is a contact sheet of every dump.
# Pack export still strips orig.png (xlat files ship).

make test-see   # replacement on/off/user + pack export (no media)
make test-vk    # headless Vulkan 1.1 buffer-copy (needs vendored headers)
```

Script grammar (`#` comments; frames absolute, any order):

```
120  tap     start
240  tap     cross 10
300  press   right
330  release right
360  capture titlescreen
400  hash
600  exit
```

Buttons: `select l3 r3 start up right down left l2 r2 l1 r1 triangle circle
cross square analog`.

Exit codes: `0` ok, `2` usage/script error, `3` setup failure, `4` stuck.

**Why it is deterministic:** `psx_run_frame()` advances a fixed cycle count from
emulated state only, and the core has no host threading (ADR-001). Same inputs
produce the same hashes — which is what makes a frame hash a usable regression
gate. Do not introduce wall-clock or thread-dependent behaviour into `psx/`; it
would silently destroy this property.

`--selftest=PATH` exercises the capture path with no BIOS and no disc, and runs
in the gate as the `qa` case. `make test-boot` is the functional counterpart:
it is **not** in the default gate (needs local media, ~40s/title) and skips
cleanly when `bios/` or `roms/` are empty.

Do **not** use `--stuck-frames` against a title screen. The Legaia menu sits
unchanged for 600+ frames; a stuck detector would flag a working boot.

Design notes: [`docs/research/20260820_1645_ai-qa-harness.md`](docs/research/20260820_1645_ai-qa-harness.md).
Functional boot (serials, golden hashes): [`docs/research/20260820_1719_qa-functional-boot.md`](docs/research/20260820_1719_qa-functional-boot.md).

## Build and verify

**First checkout — submodules are mandatory.** The build fails without them and
the upstream README never mentioned it:

```sh
git submodule update --init --depth 1 third_party/libchdr third_party/fuse-lib
git -C third_party/fuse-lib submodule update --init --recursive --depth 1
```

The second command is not optional — fuse-lib has nested submodules (`imgui`,
`cmrc`, `glad`) and CMake dies on the missing `glad/cmake` directory without it.
`third_party/SDL` is not needed; the build uses system SDL2.

```sh
# Build (Linux desktop)
./build.sh

# The gate — run before and after every change
python3 tests/run_validation.py

# Run
./bin/armsx --bios=/path/to/bios.bin --cdrom=/path/to/game.cue

# Optional headed SDL check (needs a display)
make test-sdl-runtime

# Read-only disc smoke test
make disc-probe IMAGE=/path/to/game.cue
```

The gate covers source invariants, CPU differential execution, audio timing,
the QA selftest, SEE replacement/pack unit tests, a headless Vulkan buffer
copy when headers are present, and (when `bios/`+`roms/` exist) two
enhancement-off plus two enhancement-on boots of one local title.
Cases that need cmake or SDL2 (`gpu`, `chd`, `zip`, `sdl-audio`) **skip**
instead of failing when those tools are absent. `vk` skips if
`third_party/vulkan-headers` is missing:

```sh
git clone --depth 1 --branch v1.4.309 \
    https://github.com/KhronosGroup/Vulkan-Headers.git third_party/vulkan-headers
```

### Verification assets — present locally, gitignored

`bios/` and `roms/` are gitignored. This machine has:

| | Path | Serial |
| --- | --- | --- |
| BIOS | `bios/PSX - SCPH1001.BIN` (SCPH-1001, 512 KiB) | — |
| Legend of Legaia (USA) | `roms/Legend of Legaia (USA)/Legend of Legaia (USA).cue` | SCUS-942.54 |
| Brave Fencer Musashi (USA) | `roms/Brave Fencer Musashi (USA)/Brave Fencer Musashi (USA).cue` | SLUS-007.26 |

Golden hashes live in `tests/qa/goldens.json`. `make test-boot` checks them.
**Any statement about visual quality made without a capture or a matching
hash is still unverified and must be labelled so.**

## Code conventions

- **C11** for `psx/` core, **C++** for `frontend/` glue. Match surrounding style.
- Core (`psx/`) stays free of frontend and SDL dependencies.
- No native codegen, no executable memory, no reliance on host threading
  behaviour in the CPU engines — this is a deliberate portability property of
  the fork.
- Some internal fields still use historical `psxe_*` names. Leave them.
- New Vulkan code goes behind a renderer-backend interface. Do not scatter
  Vulkan calls through `frontend/main.cpp` (already 6542 lines).

## Layout

```
tools/          armsx-qa, the headless deterministic QA driver
psx/            Emulator core (C11, no frontend deps)
  cpu.c         MIPS R3000A + complete GTE  ← PGXP tap point
  dev/gpu.c     Software rasterizer         ← parity oracle, never delete
  dev/          SPU, MDEC, DMA, CD-ROM, timers, memory cards
  input/        Controllers (sda = digital/analog pad, guncon)
frontend/       SDL2 frontend + FSUI shell (C++)
  gpu_hw.c      Shim, NOT a hardware renderer
tests/          Deterministic validation matrix
docs/           Research, decisions, worklogs  ← all work tracked here
docs/archive/   Dead material. Nothing here runs
third_party/    SDL, libchdr, fuse-lib (submodules)
```

## Known traps

- **`psx_cdrom_open()` returns 1 on SUCCESS and 0 on failure** — inverted from
  the usual C convention. `frontend/main.cpp:1988` gets this right (`== 0` is
  failure). Getting it backwards makes a bad disc path look like a successful
  open, and the emulator then spews illegal instructions at `bfc00004`, which
  reads like an emulator bug rather than a bad argument.
- `psx_take_screenshot()` and `psx_hard_reset()` are stubs that `exit(1)`
  (`psx/psx.c:199`, `psx/psx.c:189`), like the save-state pair.
- `psx/input/sda.c:124` says `// To-do: Implement analog mode`. **The comment is
  stale** — the function below it stores all four ADC axes and `sda.c:44,86`
  transmit them. Verify behaviour before "fixing" this.
- `psx_save_state` / `psx_load_state` are stubs that `log_fatal` and `exit(1)`
  (`psx/psx.c:11-21`). They are a real dependency for milestone 7.
- `frontend/config.h:25` `display_aspect` with `wide16x9` only stretches the
  output. It is **not** a widescreen hack — there is no FOV correction.
- `texture_scale_mode` (`config.h:23`) is the SDL output filter (Linear/Nearest),
  not texture filtering in the rasterizer.
- `frontend/config.c:315` has a latent `PATH_MAX` bug — the same one already
  fixed in `psx/dev/mcd.c`. It is harmless today because nothing compiles that
  file under `-std=c11`. If you add a test rule that does, add the same
  `#ifndef PATH_MAX` guard rather than rediscovering it.
- The Makefile's C rules set **no `-std=` flag**, so the app compiles as `gnu*`,
  while `tests/` rules force `-std=c11`. Code can therefore build in the app and
  fail in the gate. Always run the gate, not just `./build.sh`.
