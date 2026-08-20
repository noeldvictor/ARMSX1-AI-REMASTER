---
date: 2026-08-20 16:34
type: worklog
status: complete
tags: [ports, cleanup, qa, tooling, milestone-0]
---

# Port removal and the AI QA harness

## Goal

Two owner requests: delete `uwp/`, `psvita/`, `web/`, `ios/`; and add features
that let an AI play a game to do QA.

## Done — port removal

Supersedes ADR-001 D3 ("frozen, not deleted"). Deleted outright.

- `uwp/` (26 files), `psvita/` (6), `web/` (2), `ios/` (166) — **209 files**.
- `build-uwp-all.ps1`, `build-uwp-native.sh`, `Info.plist`,
  `frontend/vita_sdl_stubs.c`, `cmake/psvita-fsui/`.
- `tests/validate_web.py`, `tests/web_file_access.test.js`.

Consumers fixed — deleting the directories alone would have broken the gate:

- `tests/run_validation.py` — dropped the `web` case.
- `tests/validate_sources.py` — **read `web/file_access.js` and
  `ios/HostApp/Sources/AppDelegate.mm`**, so the `source` case would have failed
  too. Removed those reads and their four assertions.
- `build.sh` — removed `ios`, `wasm`, `psvita` modes and the
  `build_fsui_wasm` / `build_fsui_psvita` / `prepare_ios_sdl_package` helpers.
  `sh -n` clean.
- `Makefile` — removed the wasm/psvita/ios toolchain blocks and their variables,
  then flattened every conditional that had become statically dead
  (`WASM_TARGET`, `IOS_TARGET`, `PSVITA_TARGET`, `UWP_TARGET`). 459 → 380 lines
  before the QA additions. Zero references remain.

## Done — AI QA harness

`tools/armsx_qa.c` → `build/tools/armsx-qa` via `make qa`.

Headless, deterministic, **no SDL**. Links `psx/` plus miniz.

| Flag | Purpose |
| --- | --- |
| `--bios` / `--cdrom` / `--exe` | media; BIOS size-validated at 512 KiB |
| `--frames=N` | run N frames and exit |
| `--script=PATH` | scripted input timeline |
| `--capture-dir` / `--capture-every` | PNG frame capture |
| `--hash-every=N` | periodic frame hashes |
| `--stuck-frames=N` | fail if the image freezes |
| `--json` | one JSON object per event |
| `--selftest=PATH` | verify the capture path, no BIOS needed |

Script grammar: `<frame> press|release|tap|capture|hash|exit [args]`, `#`
comments, frames absolute and sorted on load. Unknown commands and buttons are
hard errors with `file:line`.

Exit codes: `0` ok, `2` usage/script, `3` setup, `4` stuck.

Added `tests/validate_qa.py` and a `qa` gate case that runs `--selftest` and
structurally validates the PNG.

## Bug found and fixed

**`psx_cdrom_open()` returns 1 on success, 0 on failure** — inverted from the
usual C convention. My first implementation had the polarity backwards, so a
nonexistent disc silently "opened" and the emulator spewed illegal instructions
at `bfc00004`, which reads like an emulator bug rather than a bad argument.
`frontend/main.cpp:1988` gets it right. Recorded in `CLAUDE.md` traps.

Also noted while reading: `psx_take_screenshot()` (`psx/psx.c:199`) and
`psx_hard_reset()` (`psx/psx.c:189`) are `exit(1)` stubs, like the save-state
pair.

## Verified

Everything below was executed; output is real.

- **Clean rebuild** after port removal: `./build.sh` → exit 0, `bin/armsx`
  12,472,544 bytes.
- **Full gate**: exit 0, 8 cases — `source cpu gpu audio sdl-audio chd zip qa`
  (`web` removed, `qa` added).
- **PNG writer** independently confirmed with PIL: `256x128 RGB`,
  `pixel(10,10)=(10,20,0)`, matching the generator exactly. `validate_qa.py`
  checks signature, chunk order, per-chunk CRC32, IHDR fields, zlib validity,
  decompressed length, and filter bytes.
- **Bad BIOS size** (5 bytes) → exit 3, clear message.
- **Nonexistent disc** → exit 3, clear message.
- **Blank output 60 frames** → exit 4, `stuck` reported at frame 60.
- **Malformed script line / unknown button** → exit 2 with `file:line`.

## NOT verified

**No real game has been booted.** Still no BIOS or disc on this machine.
Everything was exercised with a 512 KiB zero-filled BIOS and a junk EXE.

The harness is **structurally** verified, not **functionally** verified. Boot
behaviour, input timing, and capture of real content remain untested.

## Why this matters

`psx/` has no SDL dependency and `psx_run_frame()` advances a fixed cycle count
from emulated state alone — so a headless run is reproducible. Same inputs,
same hashes. That is what makes a frame hash a regression gate rather than a
coin flip, and it is a direct payoff from ADR-001's no-codegen,
no-host-threading constraint that was originally kept for portability.

**Do not introduce wall-clock or thread-dependent behaviour into `psx/`.** It
would silently destroy this property. Added to `CLAUDE.md`.

This harness is the milestone 1 parity gate: run one script against the software
renderer and against Vulkan, diff the hashes.

## Open questions

1. **BIOS and disc images.** Now the only thing between this harness and real
   verification. Top blocker, unchanged since the first worklog.
2. AI service and per-session budget cap (milestone 7).
3. Android NDK 27+ on this machine?

## Next

Unchanged: **milestone 1**, Vulkan backend skeleton. The parity gate now exists
in usable form, so the renderer can be checked against the software path from
the first commit rather than retrofitted.
