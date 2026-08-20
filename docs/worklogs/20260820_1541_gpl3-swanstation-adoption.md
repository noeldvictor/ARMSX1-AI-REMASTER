---
date: 2026-08-20 15:41
type: worklog
status: complete
tags: [licensing, gpl, swanstation, research, milestone-0]
---

# GPL-3.0 adoption and SwanStation evaluation

## Goal

Answer "is there open DuckStation code we can adapt?", and if so, set the
project up to use it legally.

## Done

### Correction to an earlier claim

`LICENSE` is **LGPL-3.0**, not proprietary. The README's proprietary paragraph
predated commit `90fe347 Update LICENSE` and was stale; it was carried forward
into the rewritten README earlier in this session and has been removed.

This mattered materially: LGPL-3.0 is GPL-compatible, which is the only reason
any of the below is possible.

### Research

- `docs/research/20260820_1541_adaptable-prior-art.md` — DuckStation is
  CC BY-NC-ND 4.0 (NoDerivatives) since September 2024 and unusable.
  SwanStation, the GPL-3.0 fork of its last GPLv3 commit, is usable. Survey of
  Beetle PSX, PCSX-R, PPSSPP, Dolphin, xBRZ, Real-ESRGAN with the GPLv2-only
  incompatibility trap flagged.
- `docs/research/20260820_1600_swanstation-port-plan.md` — per-file licence
  audit, component sizes, architecture gap, revised milestones.

### Decision recorded

`docs/decisions/20260820_1552_adopt-gpl3-swanstation.md` (ADR-002) — adopt
GPL-3.0, derive from SwanStation, with consequences enumerated and accepted.

### Licensing changed

- `LICENSE`: LGPL-3.0 → **GPL-3.0** (674 lines, standard FSF text).
- `LICENSES/PREVIOUS-LGPL3.txt` — prior text preserved.
- `LICENSES/SWANSTATION-GPL3.txt` — provenance and attribution.
- `README.md` and `CLAUDE.md` licence sections rewritten.
- `.gitignore` — `reference/` added.

### Reference acquired

`reference/swanstation` @ `7f69c19` (2026-08-11), 116 MB, gitignored.

## Verified

**Licence audit — actual file headers read, not repo badges:**

| File | Header | Verdict |
| --- | --- | --- |
| `pgxp.cpp` / `.h` | GPLv2 "or any later version", iCatButler 2016 | Compatible |
| `gpu_hw*.cpp` / `.h` | none; repo GPL-3.0 | Compatible |
| `texture_replacements.*` | none; repo GPL-3.0 | Compatible |

The GPLv2-only trap does not bite — PGXP is v2**+**. Had it been v2-only it
would have been unusable.

**SwanStation LICENSE:** confirmed 674-line GPL-3.0. **Last commit 2026-08-11**
— nine days old, actively maintained.

**Gate:** `python3 tests/run_validation.py` → exit 0, all 8 cases pass.

**Component inventory (measured, `wc -l`):**

```
common/vulkan/*          4,787
gpu_hw.cpp/.h            2,128
gpu_hw_vulkan.cpp/.h     4,106
gpu_hw_shadergen.cpp/.h  1,879
pgxp.cpp/.h              1,740
texture_replacements.*     325
                        ------
                        14,965
```

## Two findings that change the plan

1. **Texture replacement exists in the snapshot but is VRAM-write only.**
   `enum class ReplacmentType { VRAMWrite }` — one member. It hashes whole VRAM
   uploads (128-bit) and swaps RGBA8 images. Excellent for 2D (backgrounds,
   sprites, UI, fonts); **cannot address 3D model textures**, which live in
   texture pages sampled per-draw through CLUTs. DuckStation's per-draw
   replacement came later and is post-relicense, so it is off-limits.
   **Milestone 6 splits into 6 (2D, nearly free) and 6b (3D, original work).**

2. **PGXP is far more invasive than "tap the GTE".** `pgxp.h` exposes **44 CPU
   hook functions** — not just `MFC2`/`MTC2`/`CFC2`/`CTC2` and `LWC2`/`SWC2`,
   but ordinary `LW`/`LHx`/`SW`/`SH` too. It maintains a shadow value graph
   tracking full-precision coordinates through CPU registers and memory, keyed
   by provenance via `GetPreciseVertex(addr, value, ...)`. Hooks are needed in
   **both** CPU engines (`cached` and `interpreter`) or they diverge and the
   `cpu` differential test fails. The earlier "the GTE is the tap point" note
   was correct but incomplete. **Milestone 4 is bigger than estimated.**

## Broken / Known issues

- `bin/armsx` still **NOT VERIFIED at runtime** — never launched, no BIOS or
  discs available. Unchanged from the previous worklog.
- No SwanStation code has been ported yet. Nothing in this session touched
  `psx/` or `frontend/` beyond the earlier `mcd.c` build fix.
- The GPL-3.0 relicense is recorded but **has not been reviewed by a lawyer**.
  ADR-002 states this explicitly.

## Open questions

1. **BIOS and disc image paths.** Still the top blocker — now blocking
   milestones 3–7 *and* any visual check of ported Vulkan output.
2. AI service and per-session budget cap (milestone 7, not yet urgent).
3. Android NDK 27+ present on this machine?

## Next

**Milestone 1.** Port `src/common/vulkan/*` behind a renderer-backend interface,
Vulkan 1.1 baseline with 1.3 probing. Write the Vulkan-vs-software comparison
into `tests/gpu_renderer_parity.c` **before** the renderer, so the gate exists
before the thing it gates. Rename `USE_HARDWARE` in the same change.

---

## Addendum — SUPER ZSNES review and project reframe (16:12–16:25)

- `docs/research/20260820_1612_super-zsnes-lessons.md` — analysis of SUPER
  ZSNES (original ZSNES authors, from-scratch GPU rewrite, shipped 2026-04-27,
  v0.300 early access).
- `docs/decisions/20260820_1620_super-enhancement-engine.md` (ADR-003) — project
  reorganised around a PS1 Super Enhancement Engine.
- `README.md` and `CLAUDE.md` updated to match.

**Key finding.** Their own feature text says hi-res is *"not just an auto
upscalar"* — they built a manual drawing tool because auto-upscaling was not
good enough, and have ~10 games after ~1 year. This is the strongest evidence
against the fully-automatic premise and is now recorded as the project's
central hypothesis and primary risk rather than being glossed over.

**Roadmap changes.** Added: normal maps + per-pixel lighting (6c, prioritised
over mesh work — best value-to-risk on the board), height/parallax mapping (6d),
per-game profiles keyed by disc serial (3b, a prerequisite for everything
per-game), save states (4b), CPU overclock (7b), audio ADPCM replacement (8).
Pack format constrained to carry no PS1-derived data.

**Licensing.** SUPER ZSNES is closed source. Ideas only; no code taken or
sought. Distinct from SwanStation, which is GPL-3.0 and is a code source.

## Fixed before commit

`psx/dev/mcd.c` was accidentally converted CRLF → LF by the editing script,
turning a 7-line addition into a 285-line rewrite in the diff. Restored to CRLF;
the diff is now 8 insertions and `git blame` is preserved.

## Verified (final)

`python3 tests/run_validation.py` → exit 0, all 8 cases pass.
