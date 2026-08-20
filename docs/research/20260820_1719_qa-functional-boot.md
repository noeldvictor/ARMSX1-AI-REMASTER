---
date: 2026-08-20 17:19
type: research
status: complete
tags: [qa, bios, discs, determinism, verification]
---

# QA harness is functionally verified against two real games

## Goal

The previous session left one blocker: no BIOS, no discs, harness only
structurally tested. Media arrived. Boot both titles, measure whether frame
hashes actually repeat, and record goldens.

## Media (gitignored, not in the repo)

| Asset | Path | Notes |
| --- | --- | --- |
| BIOS | `bios/PSX - SCPH1001.BIN` | SCPH-1001 (USA), 524288 bytes. SHA-256 `71af94d1e47a68c11e8fdb9f8368040601514a42a5a399cda48c7d3bff1e99d3` |
| Legend of Legaia (USA) | `roms/Legend of Legaia (USA)/Legend of Legaia (USA).cue` | single BIN, MODE2/2352 |
| Brave Fencer Musashi (USA) | `roms/Brave Fencer Musashi (USA)/Brave Fencer Musashi (USA).cue` | 4 tracks (1 data + 3 audio) |

BIOS zip also contains SCPH-1000/1002/5500/7000/etc. USA games were booted
with SCPH-1001 only.

## What booted

Both discs opened as `CDT_LICENSED`. BIOS kernel 2.5 came up, bootstrap
loaded `SYSTEM.CNF`, and the game EXE ran. Captures were inspected, not
just hashed.

| Game | Serial (from `SYSTEM.CNF`) | Title screen | After title |
| --- | --- | --- | --- |
| Legend of Legaia | `SCUS-942.54` | frame 780–1440, 320×228, New Game / Continue | attract FMV (night village) |
| Brave Fencer Musashi | `SLUS-007.26` | frame 900/960/1080, 640×480, PUSH START BUTTON | 3D attract demo |

Sony Computer Entertainment logo is on screen by frame 120 on both games.

## Determinism — confirmed, two runs each

Every hash below matched on a second independent process. BIOS frames 60–300
are **identical across both games**, which is the expected shared BIOS
sequence and a second check that the hash is of the image, not of disc
identity.

### SCPH-1001 BIOS sequence (both games, every run)

| frame | size | hash |
| ---: | ---: | --- |
| 60 | 640×480 | `9665ce4e505de969` |
| 120 | 640×480 | `f2032c32f4d1a1ce` |
| 180 | 640×480 | `03c262677992de74` |
| 240 | 640×480 | `de6e29fed86fac80` |
| 300 | 640×480 | `449c695dc0836082` |

### SCUS-942.54 title (two runs, 960 frames, 36s wall)

| frame | size | hash | what |
| ---: | ---: | --- | --- |
| 600 | 640×480 | `32fa902e5798c87f` | PROKION logo |
| 780–1440 | 320×228 | `1c859cc02ebe806e` | title screen |

### SLUS-007.26 title (two runs, 1080 frames, 39s wall)

| frame | size | hash | what |
| ---: | ---: | --- | --- |
| 900 | 640×480 | `ba1d94ecce2c4ac2` | title, PUSH START on |
| 960 | 640×480 | `ba1d94ecce2c4ac2` | same |
| 1020 | 640×480 | `af6386f63909ffdd` | **blink — text off** |
| 1080 | 640×480 | `ba1d94ecce2c4ac2` | text on again |

Do not golden Musashi frame 1020. The start-button blink is real game
behaviour, not a hash bug.

Wall-clock for 1800 frames was ~70s on this 8-thread box (~26 emulated fps).
Title-screen scripts (840–960 frames) are ~36–40s.

## Findings that are not obvious from the code

1. **`--stuck-frames` is hostile to title screens.** Legaia's menu is a still
   image for 600+ frames. A 300-frame stuck detector would fail a working
   boot. Use it for "did the machine hang on a black screen", not for
   "reach the title and sit there".
2. **Frame 0 often reports `no display`.** The GPU has not programmed a
   display area yet. That is not a failure. Hashes start around frame 60.
3. **Resolution is part of the hash, and it moves.** BIOS is 640×480; Legaia
   title is 320×228; Musashi title is 640×480; Musashi attract is 320×240.
   A display-mode change is a real difference. Do not strip size out of the
   hash.
4. **`psx_cdrom_open` polarity is load-bearing and correct in the harness.**
   Both cues opened; inverted polarity would have looked like a kernel crash
   at `bfc00004`.
5. **CUE `FILE` names are resolved relative to the cue path**, not cwd
   (`cue_parse_file` prepends the directory). Passing the `.cue` with a
   relative path from the repo root works.

## Host constraints this session (not project decisions)

This checkout had **no cmake and no SDL2**. `./build.sh`, `make test-chd`,
`make test-zip`, `make test-gpu`, and the headed frontend all fail here.
`libvulkan1` + mesa drivers are present; Vulkan *headers* are not.

The QA driver was therefore built without libchdr: gcc + vendored
`third_party/libchdr/deps/miniz-3.1.1/miniz.c`. BIN/CUE does not need CHD.
`make qa` was changed to match, so the `qa` gate case no longer depends on
cmake.

That does **not** remove CHD from the app. It only stops the headless
harness from dragging cmake in just to deflate a PNG.

## Goldens live in-tree

`tests/qa/goldens.json` plus per-game scripts. `make test-boot` /
`python3 tests/boot_local.py` runs them and skips cleanly when media is
absent. Not in the default gate: 40s/title, and the media is local.

## NOT verified

- Headed `bin/armsx` — no SDL2 on this machine.
- CHD path through the harness — no cmake, no libchdr-static.a.
- Input scripts (`tap start` etc.) past the title. Both boots were idle
  (no pad events). Attract sequences after the title were observed, not
  scripted.
- Vulkan path — still does not exist.

## Next

Milestone 1, unchanged: Vulkan backend skeleton, with the software path as
the parity oracle. The comparison can now be a real-game hash, not just the
synthetic triangles in `tests/gpu_renderer_parity.c`.
