# ARMSX1-AI-REMASTER

An experimental fork of [ARMSX1](https://github.com/momo-AUX1/ARMSX1) building a
**Super Enhancement Engine for the PlayStation** — per-game enhancement layers
on top of an accurate emulator, driven by AI-generated assets instead of
hand-drawn ones.

HD textures, normal maps and per-pixel lighting on flat PS1 surfaces, corrected
geometry, height-mapped depth, and restored audio — generated at runtime and
cached as you play, with no manual asset preparation step.

The model is [SUPER ZSNES](https://www.zsnes.com/), which does exactly this for
the SNES. **The difference is that they hand-draw their enhancements; this
project bets that AI can generate them.** That bet may not pay off — see
[the analysis](docs/research/20260820_1612_super-zsnes-lessons.md).

---

## Please read this first

This is a personal research project, and it is genuinely important that
expectations are set honestly before anyone invests time in it.

**It is not a supported emulator, and it is not trying to become one.**

- **No support is offered.** Issues and questions will most likely go unanswered.
  That is not rudeness — there is simply no maintainer capacity here, and
  pretending otherwise would waste your time.
- **No releases, no builds, no installers.** If you want a PS1 emulator that
  works today, please use [DuckStation](https://github.com/stenzek/duckstation)
  or the [upstream ARMSX1](https://github.com/momo-AUX1/ARMSX1). Both are better
  choices for actually playing games, and this recommendation is sincere.
- **Development is deliberately AI-driven.** Most code here is written by AI
  agents under human direction. That is the experiment, not a shortcut. Code
  quality, architecture, and correctness should be read in that light.
- **Expect breakage.** `main` may not build. Features may regress. The GPU is
  being substantially rewritten and things will be broken along the way.
- **Nothing here reflects on upstream.** Bugs in this fork are this fork's. If
  you hit a problem, please do not report it to the ARMSX1 or `psxe` authors.

If you are curious, reading the code and the [`docs/`](docs/) tree is very
welcome. Just please do not depend on any of it.

---

## How this differs from upstream ARMSX1

Upstream ARMSX1 is a portable, accuracy-focused emulator that runs on desktop,
Android, iOS, UWP, WebAssembly, and PSVita, using a software rasterizer
everywhere. It is deliberately conservative: no native codegen, no executable
memory, no reliance on host threading behaviour. Those are real engineering
virtues and the reason this fork had something solid to start from.

This fork trades most of that portability for one capability upstream does not
pursue: runtime visual enhancement.

| | Upstream ARMSX1 | This fork |
| --- | --- | --- |
| **Purpose** | Portable, accurate PS1 emulation | Research into AI-driven runtime remastering |
| **Renderer** | Software rasterizer only | Software rasterizer **plus** a new Vulkan backend |
| **Graphics API** | None (SDL presentation) | Vulkan 1.1 baseline, 1.3 opportunistic |
| **Platforms** | Desktop, Android, iOS, UWP, WASM, PSVita | **Desktop Linux + Android only** |
| **Geometry** | Faithful (wobble intact) | PGXP correction planned |
| **Textures** | Native PS1 | HD replacement + live AI enhancement planned |
| **Internal res** | Native | Scalable |
| **CI** | Gitea build matrix | None, by choice |
| **Development** | Human-authored | AI-authored under human direction |
| **Support** | Maintained | None |

### What is deliberately kept

The software rasterizer stays, permanently. It carries a long tail of
game-specific compatibility fixes (see [`compat.txt`](compat.txt)) and serves as
the **parity oracle** the Vulkan renderer is validated against. When the two
disagree, the software path is treated as correct until hardware proves
otherwise. Removing it would throw away work that cannot be reconstructed.

The complete GTE implementation is likewise kept as-is — it is the tap point
PGXP needs, and it already works.

### What is frozen

`uwp/`, `psvita/`, `web/`, and `ios/` remain in the tree on the existing
software path. They are not deleted, but they will not receive remaster features
and are not verified. Reference deployment target is the **AYN Thor** handheld
(Snapdragon 8 Gen 2 / Adreno 740; Lite variant Snapdragon 865 / Adreno 650),
with Linux desktop as the development target.

### What was removed

Cloud CI, and the upstream author's funding link. Details and rationale in
[`docs/archive/ci/README.md`](docs/archive/ci/README.md). Nothing was deleted
that cannot be recovered from git history.

---

## Status

**Milestone 0 of 8 complete** — repository bootstrap, work tracking, CI removal.

The remaster features described above are **planned, not implemented.** Today
this fork behaves essentially like upstream ARMSX1. The roadmap and its
reasoning live in
[`docs/decisions/20260820_1450_remaster-architecture.md`](docs/decisions/20260820_1450_remaster-architecture.md),
and the measured starting point is in
[`docs/research/20260820_1438_duckstation-feature-gap.md`](docs/research/20260820_1438_duckstation-feature-gap.md).

| # | Milestone | Status |
| --- | --- | --- |
| 0 | Repo bootstrap, tracking, CI removal | **Complete** |
| 1 | Vulkan backend skeleton, VRAM blit parity | Not started |
| 2 | Vulkan rasterization, all primitive types | Not started |
| 3 | Internal resolution scaling | Not started |
| 3b | Per-game profile system (disc-serial keyed) | Not started |
| 4 | PGXP vertex pipeline + widescreen FOV | Not started |
| 4b | Save states (unblocks rewind, bookmarks) | Not started |
| 5 | Texture hashing, dumping, cache | Not started |
| 6 | Replacement, 2D / VRAM-write | Not started |
| 6b | Replacement, 3D texture pages | Not started |
| 6c | Normal map generation + per-pixel lighting | Not started |
| 6d | Height map / parallax mapping | Not started |
| 7 | Live async AI enhancement worker | Not started |
| 7b | CPU overclock | Not started |
| 8 | Audio: SPU ADPCM sample replacement | Not started |
| 9 | Mesh replacement research (Tripo) | Not started |

Enhancement packs will contain **no ROM or copyrighted data** — hashes and
generated assets only. You supply the game.

Every enhancement will be individually disableable. Emulated GPU results must
not change when enhancements are toggled.

---

## Inherited emulator features

These come from upstream and work today.

- CPU, DMA, GPU, SPU, MDEC, GTE, and timers
- CD-ROM loading, memory cards, BIOS selection, PS-X EXE boot
- Screenshot capture, logging controls, VSync control, fast forward
- Protocol/file launch through `armsx:///...`
- The FSUI settings shell

Disc image support: `BIN/CUE`, single-track `BIN`, `ISO`, `CHD` (including
mixed-mode metadata, pregaps/postgaps, audio byte order, and Q subchannel data),
and `ZIP` archives containing a supported image and its companion files.

Not implemented upstream and still missing here: save states, rewind, cheats,
achievements, multitap, and rumble.

### CPU engines

- `cached` (default) caches decoded instruction handlers but still performs a
  real bus fetch and opcode check per instruction. Writes invalidate matching
  entries, including cached/uncached address aliases.
- `interpreter` is the reference path, kept for diagnostics.

Neither emits native code. Select with `--cpu-engine=cached|interpreter` or
`ARMSX_CPU_ENGINE`.

### Presentation

The GPU currently uses the software PlayStation rasterizer with 16-bit VRAM
authoritative. Two SDL2 presentation modes exist: `SDL software` (default) and
`SDL accelerated` (`ARMSX_GPU_BACKEND=sdl-accelerated`), which uploads only
changed VRAM scanlines to an SDL texture.

Note that "SDL accelerated" is a *presentation* mode. It does not replace PS1
rasterization with host triangles, so switching modes does not change emulated
GPU results. The Vulkan work described above is a separate, unbuilt path.

---

## Building

Linux desktop is the only actively developed target.

Submodules must be initialised first — a clean checkout will not build without
both of these commands:

```sh
git submodule update --init --depth 1 third_party/libchdr third_party/fuse-lib
git -C third_party/fuse-lib submodule update --init --recursive --depth 1
```

Then:

```sh
./build.sh
```

Requires SDL2 development packages. Android builds need `ANDROID_NDK_ROOT`
pointing at an NDK 27+ install:

```sh
./build.sh android
```

Other targets (`macosapp`, `ios`, `wasm`, `psvita`, and the UWP scripts) are
inherited from upstream and remain in the tree, but are unverified here. Consult
upstream ARMSX1 if you need them.

## Running

```sh
./bin/armsx --bios=/path/to/bios.bin --cdrom=/path/to/game.cue
```

Or via the protocol handler on supported hosts:

```text
armsx:///absolute/path/to/game.cue
```

Settings are stored under SDL's pref path, usually
`SDL_GetPrefPath("nanodata", "armsx")`, in `settings.toml`. CLI flags override
file settings for the session.

## Validation

There is no cloud CI. Verification is local and deterministic:

```sh
python3 tests/run_validation.py
```

This covers source invariants, cached/reference CPU differential execution and
self-modifying code, GPU VRAM parity, CHD layout and subchannel logic, ZIP
extraction and traversal rejection, and browser file-selection rules.

`make disc-probe IMAGE=/path/to/game.cue` performs a read-only disc-open and
sector-read smoke test. `make test-sdl-runtime` is an optional headed check
requiring a display and an accelerated SDL driver.

---

## Documentation

All work in this repository is tracked in writing — a requirement of the
AI-driven development model, since agent sessions cannot re-read each other's
reasoning any other way.

| Path | Contents |
| --- | --- |
| [`CLAUDE.md`](CLAUDE.md) | Operating manual for AI agents |
| [`docs/README.md`](docs/README.md) | Tracking conventions and file naming |
| [`docs/research/`](docs/research/) | Investigations and feasibility studies |
| [`docs/decisions/`](docs/decisions/) | Architecture decision records |
| [`docs/worklogs/`](docs/worklogs/) | Session-by-session record of work |
| [`docs/archive/`](docs/archive/) | Removed material, kept for reference |

## License

**GPL-3.0.** See [`LICENSE`](LICENSE).

This project derives from [SwanStation](https://github.com/libretro/swanstation),
the GPL-3.0 hard fork of DuckStation's final GPL-licensed codebase. Provenance
and attribution: [`LICENSES/SWANSTATION-GPL3.txt`](LICENSES/SWANSTATION-GPL3.txt).

No code is taken from post-relicense DuckStation, which moved to CC BY-NC-ND 4.0
in September 2024 and prohibits derivative works. Only the GPL-3.0 SwanStation
lineage is used — validly licensed, because a licence already granted cannot be
retroactively revoked.

Upstream `psxe` code retains its MIT licence
([`LICENSES/UPSTREAM-MIT.txt`](LICENSES/UPSTREAM-MIT.txt)). This project was
LGPL-3.0 until 2026-08-20; that text is preserved at
[`LICENSES/PREVIOUS-LGPL3.txt`](LICENSES/PREVIOUS-LGPL3.txt) and the change is
recorded in
[ADR-002](docs/decisions/20260820_1552_adopt-gpl3-swanstation.md).

What GPL-3.0 means here, plainly: anyone may use, study, modify, and
redistribute this, including commercially. If you distribute binaries you must
offer the full corresponding source. Derivative works stay GPL-3.0.

## Acknowledgements

This fork exists only because of work done by others, and the interesting parts
of the emulator are theirs, not this fork's.

- [ARMSX1](https://github.com/momo-AUX1/ARMSX1) and its author — the portable
  base, the FSUI shell, and the multi-platform work this fork builds on
- [`psxe`](https://github.com/allkern/psxe) by Allkern — the original emulator
  and the accuracy work in the software rasterizer
- `argparse.c`, `log.c`, `tomlc99`, SDL2, FSUI donor / ImGui, libchdr
