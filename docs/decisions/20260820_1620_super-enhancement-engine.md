---
date: 2026-08-20 16:20
type: decision
status: active
tags: [architecture, enhancement-engine, roadmap, adr]
---

# ADR-003: Build a Super Enhancement Engine for PlayStation

Decision taken 2026-08-20 by the repository owner, after reviewing SUPER ZSNES.

## Context

[`20260820_1612_super-zsnes-lessons.md`](../research/20260820_1612_super-zsnes-lessons.md)
analysed SUPER ZSNES — the original ZSNES developers' from-scratch GPU rewrite,
shipped April 2026 with a per-game "Super Enhancement Engine".

It is this project's goal, already achieved, for a simpler console. The owner's
direction: **build the PlayStation equivalent.**

## Decision

The project's organising concept is now a **PS1 Super Enhancement Engine**: a
set of independently toggleable, per-game enhancement layers on top of an
accurate emulator, driven by generated assets rather than hand-drawn ones.

This supersedes nothing in ADR-001 or ADR-002 — Vulkan, PGXP, GPL-3.0 and the
platform targets all stand. It reorganises *what is built on top* and adds
enhancement types not previously considered.

### Enhancement layers, SNES → PS1

| SUPER ZSNES | PS1 equivalent | Status |
| --- | --- | --- |
| High Resolution (hand-drawn) | HD texture replacement, AI-generated + cached | Milestones 5, 6, 6b |
| Texture / Normal Map | **Normal maps + per-pixel lighting on flat PS1 surfaces** | **NEW — high priority** |
| 3D (Mode 7 height maps) | **Parallax / relief mapping from generated height maps** | **NEW — tractable 3D** |
| Overclock | CPU overclock (`PSX_CPU_CPS` is currently a fixed constant) | **NEW** |
| Widescreen | PGXP-based FOV correction | Milestone 4 |
| Uncompressed Audio | SPU ADPCM sample replacement | **NEW — own track** |
| Per-game config | Profiles keyed by disc serial (`SLUS-`/`SLES-`/`SCUS-`) | **NEW — prerequisite** |
| All individually disableable | Same. Already non-negotiable #2 | Existing |

### Two additions that change priorities

**Normal maps move ahead of mesh replacement.** Generating a normal map from an
albedo texture is a well-solved image-to-image problem needing no object
identity, no rigging, and no animation matching. PS1 surfaces have *no*
per-pixel lighting, so adding normals plus a light source makes geometry read as
three-dimensional without touching a vertex. Best value-to-risk ratio available.

**Height/parallax mapping is the tractable 3D middle ground** between "textures
only" and track-3 mesh replacement, and carries none of the identification
problem.

### Per-game profiles are a prerequisite

Nothing above is per-game without them. ARMSX1 has `compat.txt` but no
mechanism. Keyed by disc serial. This is now an early milestone.

### Pack format constraint

Following their model — **enhancement packs contain no ROM or copyrighted
data.** Our cache stores hashes plus generated assets only, never PS1-derived
imagery or audio. This is a day-one constraint on the format, not something to
retrofit, and it is what makes packs shareable.

## The central hypothesis, stated plainly

SUPER ZSNES's own description says their hi-res mode is **"not just an auto
upscalar"** — they built a manual drawing tool because automatic upscaling was
not good enough, and have ~10 games after roughly a year.

> **This project's bet is that AI closes the gap between automatic and
> hand-drawn quality.**

That is the hypothesis. It may fail. If it does, the fallback is their model — a
curation tool plus a per-game pack format — which is a worthwhile artifact
either way, and which the architecture above produces regardless.

Two experienced authors needed a year to reach v0.300 on the *easier* console.
PS1 is harder: 3D geometry instead of tile layers, no fixed tile grid, and
textures in VRAM pages sampled through CLUTs rather than as addressable assets.
**One convincingly enhanced PS1 game is a real result.**

## Licensing

SUPER ZSNES is closed source. **No code is taken and none will be sought.**
Ideas and architecture are not copyrightable; implementations are. Everything
here is independently implemented.

## Revised roadmap

| # | Deliverable |
| --- | --- |
| 0 | Repo bootstrap, tracking, CI removal — **done** |
| 1 | Vulkan backend skeleton, VRAM blit parity |
| 2 | Vulkan rasterization, all primitive types |
| 3 | Internal resolution scaling |
| 3b | **Per-game profile system (disc serial keyed)** |
| 4 | PGXP vertex pipeline + widescreen FOV |
| 4b | **Save states** (unblocks rewind, bookmarks, AI cache reproducibility) |
| 5 | Texture hashing, dumping, cache |
| 6 | Replacement, 2D / VRAM-write |
| 6b | Replacement, 3D texture pages — original work |
| 6c | **Normal map generation + per-pixel lighting** |
| 6d | **Height map / parallax mapping** |
| 7 | Live async AI enhancement worker |
| 7b | **CPU overclock** |
| 8 | Audio: SPU ADPCM sample replacement |
| 9 | Mesh fingerprinting research (Tripo) — may fail, documented either way |
