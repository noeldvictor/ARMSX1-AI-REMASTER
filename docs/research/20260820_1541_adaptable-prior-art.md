---
date: 2026-08-20 15:41
type: research
status: complete
tags: [licensing, prior-art, vulkan, pgxp, duckstation, swanstation]
---

# Can we adapt DuckStation code? And what else is adaptable?

## Question

Milestones 1–6 are a Vulkan renderer, PGXP, internal resolution scaling, and
texture replacement. DuckStation has all four, working. Can we take them?

## Answer

**DuckStation itself: no.** A GPLv3 fork of it: **yes, and it is the best
option available.**

## This repo's own license — corrected

Prerequisite for everything below, and it was previously recorded wrong.

`LICENSE` is **LGPL-3.0**. 165 lines, pure LGPL text, no mention of
"proprietary", "all rights reserved", or "Moon". Git history:

```
dee7345 Create LICENSE
17b97e4 Make Moon-owned ARMSX material proprietary
90fe347 Update LICENSE          ← replaced with LGPL-3.0
```

`README.md` continued to describe the project as proprietary/all-rights-reserved
after `90fe347`, contradicting the licence file. That stale text has been
removed; `LICENSE` is the operative document.

This matters enormously, because **LGPL-3.0 is GPL-compatible.** LGPLv3 §2
explicitly permits conveying a covered work under GPLv3. Had the project
actually been proprietary, every option below would be closed. It is not, so
they are open.

*Not legal advice. If anything commercial is ever planned, get a real opinion
before merging copyleft code.*

## DuckStation: closed

DuckStation relicensed **away** from open source in September 2024 — first to
PolyForm Strict on 1 September, then to **CC BY-NC-ND 4.0** on 13 September.

**ND = NoDerivatives.** That is not a grey area or a nuance to work around: it
prohibits adapted works outright. Even distro packaging is treated as a derived
work under it. Current DuckStation source is readable, and nothing more.

The relicense was contested — it was done without obtaining agreement from all
prior contributors, which is why the GPL-era snapshot's status is what it is:

**A licence already granted cannot be retroactively revoked.** Code released
under GPLv3 stays available under GPLv3 to everyone who received it. Forks taken
at or before the last GPLv3 commit remain legitimately GPLv3.

## SwanStation: the answer

[`libretro/swanstation`](https://github.com/libretro/swanstation) is a hard fork
of DuckStation taken from the last GPLv3 codebase, maintained precisely so a
fully open PS1 core continues to exist.

- **Licence: GPL-3.0** — compatible with our LGPL-3.0.
- **~6,161 commits on main.** Not an abandoned snapshot.
- **Hardware renderers: Vulkan, OpenGL, D3D11**, plus software.
- **PGXP** — geometry precision and perspective-correct texturing.
- **Upscaling**, texture filtering, 24-bit colour, post-processing shader chains.
- Dynarec for x86-64, ARMv7, AArch64 (not wanted here — the fork deliberately
  emits no native code — but it is there).

This is, almost exactly, milestones 1–4 already written and shipped. Its Vulkan
backend and PGXP module are the two most valuable pieces.

## Other viable sources

| Project | Licence | Offers | Usable? |
| --- | --- | --- | --- |
| **SwanStation** | GPL-3.0 | Vulkan renderer, PGXP, upscaling, 24-bit | **Yes — primary source** |
| **Beetle PSX HW** | GPLv2 | Vulkan renderer (TinyTiger), PGXP (iCatButler) | **Verify first** — see below |
| **PCSX-R + PGXP** | GPLv2(+?) | Original PGXP by iCatButler | Verify; algorithm is documented |
| **PPSSPP** | GPLv2+ | Mature texture dump/replace system | Yes, and `+` makes it clean |
| **Dolphin** | GPLv2+ | Texture dump/replace, custom shaders | Yes, mainly as design reference |
| **xBRZ** | GPLv3 | Algorithmic upscaler | Yes |
| **Real-ESRGAN** | BSD-3 | AI upscaling | Yes — permissive, no copyleft |

### The GPLv2 trap

**GPLv2-*only* is incompatible with GPLv3.** They cannot be combined. Beetle PSX
and PCSX-R are commonly described as "GPLv2", and Mednafen — Beetle's ancestor —
is GPLv2. If any file is v2-only rather than "v2 or later", it cannot be merged
into an LGPLv3/GPLv3 work at all.

**Check the actual per-file headers before copying anything from these.** Do not
rely on the repo's headline licence badge. PPSSPP and Dolphin being GPLv2**+**
is what makes them safe by comparison.

## The consequence you are buying

Merging GPLv3 code **upgrades the whole combined work to GPL-3.0.** This is not
optional and not reversible by later removing the code.

Practically:

- The project becomes GPL-3.0, not LGPL-3.0.
- Full corresponding source must be offered to anyone you distribute binaries to.
- Any future proprietary or commercial-closed plan for this codebase ends.
- Upstream `psxe`'s MIT portions are unaffected — MIT is permissive and absorbs
  into GPL fine. Only Moon-owned/ARMSX-specific material is affected, and that
  is already LGPL-3.0.

For a personal AI-development experiment this is a low cost. It should still be
a deliberate decision, recorded in an ADR, rather than something that happens by
accident during a copy-paste.

## Recommendation

**Study SwanStation; port deliberately; do not paste.**

Adapting is not copy-paste regardless of licence. SwanStation is a libretro core
built on DuckStation's `GPUHWBackend` architecture with its own VRAM model,
shader-gen layer, and C++ idioms. ARMSX1 is C11 core + SDL2 C++ frontend with a
different GPU structure entirely. Dropping `gpu_hw_vulkan.cpp` into this tree
will not compile and would not be worth debugging if it did.

What it is genuinely worth:

1. **Milestone 1–3 (Vulkan renderer, upscaling)** — read SwanStation's Vulkan
   backend for the hard-won parts: VRAM-as-texture handling, readback for
   VRAM-to-CPU ops, the mask-bit/semi-transparency pipeline states, and how it
   keeps native-res VRAM coherent while rendering upscaled. Those are the
   problems that cost weeks to rediscover.
2. **Milestone 4 (PGXP)** — the most directly liftable piece. PGXP is fairly
   self-contained: tap the GTE, keep a vertex cache keyed by CPU register/memory
   provenance, carry floats to the rasterizer. Our GTE is complete
   (`psx/cpu.c:92-110`), so the tap point already exists.
3. **Milestones 5–6 (texture replacement)** — study **PPSSPP** rather than
   SwanStation. Its texture-replacement system is more mature and its hashing
   and cache-format decisions are exactly the ones we face. GPLv2+ so the
   licence is clean.

Attribution and licence headers must be preserved on anything actually derived.
That obligation is what the DuckStation relicense was reportedly a reaction to,
so it is worth honouring properly.

## Effect on the roadmap

This does not change the milestone order, but it substantially de-risks it.
Milestones 1–4 shift from "invent it" to "port a known-good design", which is a
different and much better problem for AI-driven development — there is a
reference implementation to check behaviour against, in addition to our own
software rasterizer as the parity oracle.

Track 3 (mesh replacement) has **no prior art anywhere**. It stays research.

## Open decision

**Accept GPL-3.0 for the project?** Required before any SwanStation-derived code
lands. Needs an ADR either way. Until then, milestone 1 should be written
independently — reading for architecture is fine, deriving is not.

## Sources

- [DuckStation licence change coverage — GamingOnLinux](https://www.gamingonlinux.com/2024/09/playstation-1-emulator-duckstation-changes-license-for-no-commercial-use-and-no-derivatives/)
- [Time Extension — relicense fallout](https://www.timeextension.com/news/2024/09/creator-of-ps1-emulator-duckstation-threatens-to-shut-the-whole-thing-down-following-license-change)
- [GPL violation analysis — Leah Rowe](https://vimuser.org/duckstation.html)
- [libretro/swanstation](https://github.com/libretro/swanstation)
- [Beetle PSX HW — libretro docs](https://docs.libretro.com/library/beetle_psx_hw/)
- [Introducing Vulkan PSX renderer for Beetle/Mednafen PSX](https://www.libretro.com/index.php/introducing-vulkan-psx-renderer-for-beetlemednafen-psx/)
