# AGENTS.md

Operating manual for AI agents working in this repository. Read this before
touching anything. `CLAUDE.md` is a pointer here.

## What this repo is

`ARMSX1-AI-REMASTER` is a personal, experimental fork of
[ARMSX1](https://github.com/momo-AUX1/ARMSX1) (itself a fork of `psxe`). It is a
PlayStation 1 emulator being rebuilt to **auto-remaster games while you play
them** — HD textures, translations, corrected geometry, and possibly replaced
meshes, produced algorithmically and by AI APIs at runtime.

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
one and add `Superseded by:` to the old one. Never rewrite history.

Full conventions and the worklog template: [`docs/README.md`](docs/README.md).

## The goal, stated precisely

A **Super Enhancement Engine for PlayStation** (ADR-003) — per-game,
individually toggleable enhancement layers over an accurate emulator:

1. **Corrected geometry** — no polygon wobble, no swimming textures (PGXP).
2. **HD textures** — upscaled algorithmically, then AI-enhanced, cached on first
   sighting.
3. **Translations** — same dump/hash folders; paint `xlat-<lang>.png` over
   `orig.png`. PS1 text is pixels, not a string table.
4. **Normal maps + per-pixel lighting** — PS1 surfaces have none. Highest
   value-to-risk ratio on the remaining roadmap; prioritise over mesh work.
5. **Height / parallax mapping** — apparent geometric detail with no mesh
   identification problem.
6. **Audio** — SPU ADPCM sample replacement.
7. **Mesh replacement** — research only, may fail.

All appearing *during play*, with no mandatory manual asset-preparation step.
Dumps from one playthrough are reused; HD and translations do not require a
second full play.

### The central hypothesis — and the honest risk

The model is [SUPER ZSNES](https://www.zsnes.com/). Their hi-res mode is **"not
just an auto upscalar"** — they built a drawing tool because auto-upscaling was
not good enough.

**This project's bet is that AI closes the gap between automatic and hand-drawn
quality.** It may fail. The fallback is their model: a curation tool plus a
per-game pack format, which this architecture produces anyway.

Do not write documentation or worklogs that assume the bet has been won. PS1 is
*harder* than SNES here: 3D geometry rather than tile layers, no fixed tile
grid, textures in VRAM pages sampled through CLUTs. **One convincingly enhanced
game is a real result.**

**The risk is hedged** (ADR-004). AI does a first pass; the human touches up in
their own image editor. At 70% AI quality this still wins; at 20% it degrades
into SUPER ZSNES's model with a draft instead of a blank canvas.

### Human-in-the-loop pipeline (ADR-004)

- **We do not build an image editor.** Assets are PNGs. The user edits them in
  Photoshop/Krita/Aseprite. Our surface is a file watcher, a review queue, a
  dump catalog, and an A/B hotkey.
- **Auto-apply everything.** AI/algorithmic output goes live immediately.
  Garbled fonts and logos *will* appear in-game until fixed — accepted trade.
- **Resolution order at draw time:** `xlat-<lang>.png` → `user.png` →
  `generated.png` → original VRAM. (`generated.png` is the AI/algorithmic slot;
  ADR-004 called it `ai.png`.)
- **`reverted`, `edited`, `user.png`, and any `xlat-*.png` LOCK an asset**
  against further AI passes. Without this, auto-apply re-enhances rejected or
  translated assets forever. This is load-bearing.

### Pack format constraint (day one)

Enhancement packs carry **hashes plus generated / user / xlat assets only —
never PS1-derived imagery or audio.**

**The local cache keeps `orig.png` (the editor needs it as reference); the pack
export step deletes it.** Getting this backwards makes packs unshippable.

### Hot reload traps

- **Debounce the watcher.** Image editors write in several syscalls.
- A malformed or partial PNG must **keep the previous texture**.
- Decode off-thread; swap on the frame boundary.

## Non-negotiables

1. **The software rasterizer is the parity oracle. Never delete it, never
   "replace" it.** `psx/dev/gpu.c` encodes years of game-specific fixes — see
   `compat.txt` and comments like *"Fixes Mortal Kombat II, Bubble Bobble,
   Driver 1 & 2"*. The Vulkan renderer is validated *against* it. If Vulkan and
   software disagree, **software is right** until proven otherwise on hardware.

2. **Accuracy is not traded for looks.** Enhancement is a presentation layer.
   Emulated GPU results must not change when enhancements are toggled off. If a
   remaster feature requires changing emulation, it needs an explicit toggle
   defaulting to off. `texel_cb` is installed only when enhance is on.

3. **Every session writes a worklog.** No exceptions.

4. **Never claim something works without running it.** If you did not execute
   it, write "NOT VERIFIED".

5. **No cloud CI. Ever.** Do not add `.github/workflows/`. Verification is local.

## Architecture you must know before editing the GPU

Read [`docs/research/20260820_1438_duckstation-feature-gap.md`](docs/research/20260820_1438_duckstation-feature-gap.md)
in full before your first GPU change. The essentials:

- **`frontend/gpu_hw.c` is not a hardware renderer.** It is a shim that forwards
  to the software rasterizer. `USE_GPU_BACKEND` is defined unconditionally in
  the Makefile. It is a triangle-dispatch hook plus SDL presentation selection,
  not GPU-accelerated rasterization. The old name `USE_HARDWARE` was a lie and
  must not return.
- **"SDL accelerated" is a presentation mode**, not a renderer.
- **Vertices reaching the rasterizer are integer 2D screen coordinates** —
  `int16_t x, y` with `uint8_t tx, ty`. No Z, no W. This is why PS1 games wobble
  and why textures swim. It cannot be fixed downstream.
- **The GTE is complete.** It is the PGXP tap point. Do not rewrite it.
- **VRAM is authoritative**: 1024×512 × 16-bit.
- **Dump observers must not write VRAM.** `see_on_vram_write` / `see_on_texture_use`
  read only. The `vram-unchanged` SEE test is load-bearing.

## Where the code actually is (2026-08-22)

Do not trust the original "next: milestone 1" line. Landed vs not:

| | Status |
| --- | --- |
| Milestone 0 bootstrap | done |
| Headless QA + golden boots (Legaia / Musashi) | done |
| SEE 2D present replacement + pack export | done |
| Texture-page dump on sample + `see_enhance_cache` | done |
| Translation slot `xlat-<lang>.png` + `catalog.html` | done |
| Vulkan 1.1 buffer copy of software VRAM | done |
| Vulkan graphics-pipeline triangle rasterizer | **code exists**; Haswell ANV returns `vkCreateGraphicsPipelines` **-13**. Device skip, not a blit standing in for rasterization. Pixel memcmp vs software is **NOT VERIFIED** on this host. |
| Swapchain / headed present | not done (no `sdl2-config` here) |
| PGXP, internal res, normal/parallax maps, live AI worker, save states | not done |
| SDL frontend SEE hooks | not done |

Haswell Mesa prints `Haswell Vulkan support is incomplete`. Buffer copy works.
A trivial RGBA8 pipeline also fails. That is an environment skip with captured
`VkResult`, not a reason to fake parity via `vk_copy_software_vram`.

Makefile falls back to gitignored `third_party/cmake/bin/cmake` when PATH has
no cmake, so `chd`/`zip` gate cases run instead of false-failing.

## Decisions already made — do not relitigate

[`docs/decisions/20260820_1450_remaster-architecture.md`](docs/decisions/20260820_1450_remaster-architecture.md)

| | Decision |
| --- | --- |
| Render path | Full DuckStation-class rewrite: Vulkan + PGXP + internal scaling + texture replacement |
| Graphics API | **Vulkan only.** Baseline 1.1 core, 1.3 opportunistic behind capability checks |
| Platforms | **Desktop Linux + Android only.** Reference device: AYN Thor |
| Removed ports | `uwp/`, `psvita/`, `web/`, `ios/` deleted 2026-08-20 |
| AI timing | Live async with progressive disk cache. Algorithmic first, AI as an upgrade pass |
| 3D scope | Textures + PGXP ship. Mesh replacement is research that never blocks the others |
| Verification | Headless parity gate; real discs for visual spot-checks |

If you believe a decision is wrong, **write a new ADR**. Do not quietly build
something else.

## Licensing — read before copying any code

**GPL-3.0** (ADR-002). Was LGPL-3.0; previous text at `LICENSES/PREVIOUS-LGPL3.txt`.

- **NEVER copy from `stenzek/duckstation`.** CC BY-NC-ND 4.0. NoDerivatives.
- **DO use `reference/swanstation`** — GPL-3.0, gitignored, pinned at `7f69c19`.
- **Verify per-file headers, not the repo badge.** GPLv2-**only** is incompatible
  with GPLv3. SwanStation `pgxp.cpp`/`.h` are GPLv2-**or-later** (fine).
- Preserve attribution headers on every derived file.
- DuckStation added per-draw texture-page replacement **after** the relicense.
  That work is CC BY-NC-ND. **Do not look at it.** 3D texture-page replacement
  here is original (`see_on_texture_use`).

Porting is **reimplementation against a reference**, not a merge. Do not port
the dynarec, the libretro host, or D3D/OpenGL backends.

## SUPER ZSNES is closed source

**Take no code from it and do not seek any.** Ideas are not copyrightable;
implementations are.

## Working session protocol

**Start:**
1. Read the most recent file in `docs/worklogs/`.
2. Read any `docs/research/` doc relevant to what you are about to touch.
3. State which milestone you are working on.

**During:**
- Small, verifiable steps. Run the gate often.
- Non-obvious discoveries go in `docs/research/`, not code comments.

**End:**
1. Write `docs/worklogs/YYYYMMDD_HHMM_slug.md`.
2. Run the gate and record the actual output.
3. State the single most useful next action.

## Super Enhancement Engine (how to dump, HD, translate)

`enhance/see.c` is the presentation layer. It never writes `gpu->vram`.

```
cache/<serial>/<hash>/
  orig.png          dumped original (local only)
  meta.json         texpage coords when kind=texpage
  generated.png     algorithmic/AI HD
  user.png          human HD touch-up
  xlat-en.png       translation (any xlat-XX.png)
  reverted | edited lock files
cache/<serial>/catalog.html    contact sheet of orig.png
cache/<serial>/dumps.jsonl     append-only dump index
```

- Dump runs even with enhance **off** (VRAM-write, texture-use, present ingest).
- `see_enhance_cache()` writes `generated.png` for every unlocked `orig.png`
  **without playing again**.
- Drop `user.png` or `xlat-en.png` in the hash folder; next boot uses it.
- `--enhance --lang=en` looks up `xlat-en.png` first.
- Pack export copies generated/user/xlat and **strips orig.png**.

PS1 dialogue/UI is painted on texture pages. There is no string extractor.
Translators edit `orig.png` in their own editor.

## Driving a real game (AI QA harness)

`build/tools/armsx-qa` (via `make qa`) runs a game **headless and
deterministically** — no window, no audio device, no host input.

```sh
make qa
make test-boot          # golden hashes; skips if bios/roms empty (~40s/title)

./build/tools/armsx-qa --bios="bios/PSX - SCPH1001.BIN" \
    --cdrom="roms/Legend of Legaia (USA)/Legend of Legaia (USA).cue" \
    --script=tests/qa/scus-942.54-title.qa --json

# enhancement on (presentation only; VRAM texture pages unchanged)
./build/tools/armsx-qa --bios="bios/PSX - SCPH1001.BIN" \
    --cdrom="roms/Legend of Legaia (USA)/Legend of Legaia (USA).cue" \
    --script=tests/qa/scus-942.54-title.qa --enhance --enhance-dir=cache --lang=en --json

make test-see
make test-vk
```

Script grammar (`#` comments; frames absolute):

```
240  hash
840  capture titlescreen
900  exit
```

Buttons: `select l3 r3 start up right down left l2 r2 l1 r1 triangle circle
cross square analog`.

Exit codes: `0` ok, `2` usage/script error, `3` setup failure, `4` stuck.

`psx_run_frame()` advances a fixed cycle count from emulated state only. Do not
introduce wall-clock or thread-dependent behaviour into `psx/`.

Do **not** use `--stuck-frames` against a title screen. The Legaia menu sits
unchanged for 600+ frames.

Goldens: `tests/qa/goldens.json`. Legaia title (enhance off):

| frame | size | hash | note |
| --- | --- | --- | --- |
| 240 | 640×480 | `de6e29fed86fac80` | SCPH-1001 Sony logo |
| 840 | 320×228 | `1c859cc02ebe806e` | New Game / Continue |

## Build and verify

**First checkout — submodules are mandatory:**

```sh
git submodule update --init --depth 1 third_party/libchdr third_party/fuse-lib
git -C third_party/fuse-lib submodule update --init --recursive --depth 1
```

```sh
./build.sh                          # needs system SDL2
python3 tests/run_validation.py     # the gate; run before and after changes
./bin/armsx --bios=... --cdrom=...
```

The gate covers source invariants, CPU differential, audio timing, QA selftest,
SEE unit/pack tests, headless Vulkan when headers exist, and (when `bios/`+
`roms/` exist) two enhance-off plus two enhance-on boots. Cases that need cmake
or SDL2 skip when those tools are absent. Vendored cmake is used if present
under `third_party/cmake/bin/cmake`. `vk` skips if Vulkan headers are missing:

```sh
git clone --depth 1 --branch v1.4.309 \
    https://github.com/KhronosGroup/Vulkan-Headers.git third_party/vulkan-headers
```

### Local gitignored media

| | Path | Serial |
| --- | --- | --- |
| BIOS | `bios/PSX - SCPH1001.BIN` (SCPH-1001, 512 KiB) | — |
| Legend of Legaia (USA) | `roms/Legend of Legaia (USA)/Legend of Legaia (USA).cue` | SCUS-942.54 |
| Brave Fencer Musashi (USA) | `roms/Brave Fencer Musashi (USA)/Brave Fencer Musashi (USA).cue` | SLUS-007.26 |

**Any statement about visual quality made without a capture or a matching hash
is still unverified and must be labelled so.**

## Code conventions

- **C11** for `psx/` core, **C++** for `frontend/` glue.
- Core (`psx/`) stays free of frontend and SDL dependencies.
- No native codegen, no executable memory, no host-thread dependency in the CPU
  engines (ADR-001).
- Historical `psxe_*` field names stay.
- New Vulkan code goes in `vk/`, behind a renderer-backend interface. Do not
  scatter Vulkan calls through `frontend/main.cpp`.
- Preserve original line endings when editing mixed CRLF files (`psx/dev/gpu.c`).

## Layout

```
tools/          armsx-qa, headless deterministic QA driver
psx/            Emulator core (C11, no frontend deps)
  cpu.c         MIPS R3000A + complete GTE  ← PGXP tap point
  dev/gpu.c     Software rasterizer         ← parity oracle, never delete
enhance/        Super Enhancement Engine (dump, HD, xlat, pack)
vk/             Headless Vulkan blit + triangle rasterizer (no WSI)
frontend/       SDL2 frontend + FSUI shell
  gpu_hw.c      Shim, NOT a hardware renderer
tests/          Deterministic validation matrix
docs/           Research, decisions, worklogs
cache/          Local dumps (gitignored)
third_party/    libchdr, fuse-lib; optional gitignored cmake + vulkan-headers
```

## Known traps

- **`psx_cdrom_open()` returns 1 on SUCCESS and 0 on failure.**
  `frontend/main.cpp` treats `== 0` as failure. Getting it backwards makes a
  bad disc path look like a successful open, then illegal instructions at
  `bfc00004`.
- `psx_take_screenshot()`, `psx_hard_reset()`, `psx_save_state` /
  `psx_load_state` stub and `exit(1)` / `log_fatal`. Save states are a real
  dependency for milestone 7.
- `psx/input/sda.c:124` `// To-do: Implement analog mode` is **stale** — analog
  axes are stored and transmitted. Verify before "fixing".
- `display_aspect` / `wide16x9` only stretches output. Not a widescreen FOV hack.
- `texture_scale_mode` is the SDL output filter, not rasterizer filtering.
- `frontend/config.c` has a latent `PATH_MAX` bug (same class as `mcd.c`).
- App C rules set **no `-std=`**; `tests/` force `-std=c11`. Always run the
  gate, not just `./build.sh`.
- Makefile `gpu` / `sdl-audio` skip without `sdl2-config`. Headed `./bin/armsx`
  is not the verification path on this host — `armsx-qa` is.

## Milestones (roadmap; several already have a vertical)

| # | Deliverable | Gate |
| --- | --- | --- |
| 0 | Repo bootstrap, tracking, CI removal | done |
| 1 | Vulkan backend skeleton, VRAM blit parity | blit done; no swapchain |
| 2 | Vulkan rasterization, all primitive types | triangle path coded; pixels unverified on Haswell |
| 3 | Internal resolution scaling | Visual + parity at 1x |
| 3b | Per-game profile system (disc-serial keyed) | Unit |
| 4 | PGXP vertex pipeline + widescreen FOV | Wobble gone, no regressions |
| 4b | Save states | Round-trip determinism |
| 5 | Texture hashing, dumping, cache | dumps exist (2D + texpage) |
| 5b | Cache format, manifest, asset state machine | partial (catalog + dumps.jsonl) |
| 6 | Replacement, 2D / VRAM-write | HD pack renders |
| 6b | Replacement, 3D texture pages — original work | dump + native UV sample; not hi-res 3D |
| 6c–6e | Normal maps, parallax, A/B hotkey | Visual |
| 7–7e | Live AI worker, overclock, watcher, review UI, pack export | pack export done |
| 8 | Audio: SPU ADPCM sample replacement | Visual/aural |
| 9 | Mesh fingerprinting research | Written up either way |

## Useful next actions (pick one)

1. Run `make test-vk` on a GPU that can create Vulkan graphics pipelines and
   memcmp software vs `vk_raster_triangle`.
2. Wire SEE dump/replace/`--lang` into the SDL frontend when SDL2 is present.
3. Open `cache/SCUS-942.54/catalog.html`, drop a `xlat-en.png`, boot with
   `--enhance --lang=en`.
