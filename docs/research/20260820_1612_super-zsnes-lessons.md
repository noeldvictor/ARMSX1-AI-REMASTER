---
date: 2026-08-20 16:12
type: research
status: complete
tags: [super-zsnes, prior-art, enhancement-engine, strategy]
---

# SUPER ZSNES: the closest existing proof of concept

## Why this matters

**SUPER ZSNES is this project's idea, already shipped, for the SNES.** Released
27 April 2026 by the two original ZSNES developers, rewritten from scratch as a
GPU-driven emulator with a "Super Enhancement Engine". Currently v0.300, early
access, on Windows/Mac/Linux/Android with iOS pending.

It is the single most relevant prior art that exists, and it both validates the
goal and challenges the method.

## Licensing — ideas only

Original ZSNES was GPLv2. **SUPER ZSNES is a from-scratch rewrite, distributed
through app stores with a Patreon, and is not open source.** No source is
available and none should be sought.

**Take no code. Ideas and architecture are not copyrightable; implementations
are.** Everything below is design inspiration, independently implemented.

## What their Super Enhancement Engine does

Per-game enhancements, currently covering ~10 titles (Chrono Trigger, F-Zero,
Gradius 3, Mega Man X, Super Castlevania 4, Super Ghouls & Ghosts, Super Mario
Kart, Super Mario World, Super Metroid, Zelda: A Link to the Past):

| Feature | Description |
| --- | --- |
| High Resolution | Hand-redrawn hi-res detail, **not** auto-upscaling |
| Texture / Normal Map | Normal maps added to backgrounds for depth |
| Overclock | Per-game, to remove native slowdown |
| Widescreen | Where the game internally supports it |
| Uncompressed Audio | Curated replacements for compressed samples |
| 3D | Mode 7 tiles replaced with 3D **height-mapped** data |

Plus: all enhancements individually disableable, per-game config, and
**"Enhancement data contains no ROM or copyrighted data."**

## The finding that challenges our premise

Directly from their feature description:

> **High Resolution — Not just an auto upscalar**, but an internal drawing
> program is used to make sure that the higher resolution details can be
> manually drawn to look nice and crisp.

Two experienced emulator authors, building exactly this, **explicitly rejected
automatic upscaling as insufficient** and built a manual drawing tool instead.
After roughly a year they have ten games.

This is the strongest available evidence against the "fully automatic" premise
in ADR-001, and it must not be waved away.

**It does not kill the project — it sharpens it.** The honest framing:

> SUPER ZSNES proves the *output* is worth having, and demonstrates that hand
> curation achieves it. **This project's bet is that AI closes the gap between
> automatic and hand-drawn quality.** If that bet fails, the fallback is their
> model — a curation tool plus a per-game pack format — which is a useful
> artifact regardless.

Their v0.300 also notes a further trap worth recording: they added
"Shadow/Highlights ... should help with the **washed out look**". Naive
enhancement flattens contrast. Expect this and plan for it.

## What to steal outright

### 1. Normal maps — the highest value/effort ratio available

Generating a normal map from an albedo texture is **far more tractable than mesh
generation** — it is a well-solved image-to-image problem, achievable
algorithmically or with a small model, and needs no object identity, no rigging,
and no animation matching.

On PS1 this is arguably a bigger win than on SNES: PS1 surfaces are flat-shaded
or vertex-lit with no per-pixel lighting at all. Adding normal maps plus a light
source makes geometry read as three-dimensional without touching a single vertex.

**This belongs on the roadmap ahead of mesh replacement.** It delivers much of
the perceived "remaster" effect at a fraction of the risk.

### 2. Height maps instead of meshes

Their 3D mode replaces Mode 7 tiles with height-mapped data rather than authored
models. The generalisation — **parallax/relief mapping driven by a generated
height map** — gives apparent geometric detail with no mesh identification
problem at all.

This is the tractable middle ground between "textures only" and the track-3 mesh
replacement research, and it should be its own milestone.

### 3. Per-game enhancement profiles

Keyed by disc serial (`SLUS-xxxxx` / `SLES-xxxxx` / `SCUS-xxxxx`). ARMSX1 has
`compat.txt` but no per-game settings mechanism at all. Everything else here
depends on having one.

### 4. Enhancement packs carry no copyrighted data

Their packs ship enhancement data only; the user supplies the ROM. Our cache
must follow the same rule: **hashes plus generated assets, never PS1 data**.
This keeps a shareable pack format legally clean and is a constraint on the
cache format from day one.

### 5. Audio is an enhancement surface we had not considered

PS1 audio is SPU ADPCM — heavily compressed, and a real part of why games sound
dated. Uncompressed/AI-restored sample replacement is a legitimate track, and
one where generative audio is more mature than generative 3D.

### 6. Everything individually disableable

Matches non-negotiable #2 in `CLAUDE.md` already. Their shipping product
confirms it is the right default.

### 7. Quality-of-life we simply lack

They ship: save states, **rewind**, auto-save history, save bookmarks, cheat
codes, quick load, per-game overclock, RetroAchievements (planned).

ARMSX1 has **none** of these — `psx_save_state` still calls `exit(1)`
(`psx/psx.c:11-21`). Save states are already a milestone-7 dependency; rewind
and bookmarks are cheap once states exist.

## Realistic expectations

Two developers who wrote the original emulator, working full-time-ish with
Patreon support, produced in ~1 year: a from-scratch GPU-driven core, ~10
enhanced games, and a v0.300 that still describes itself as early access with
"performance may be a bit slow" and incomplete special-chip support.

**PS1 is harder than SNES for this**: 3D geometry rather than tile layers, no
fixed tile grid to key on, and textures that live in VRAM pages sampled through
CLUTs rather than as discrete addressable assets.

Plan accordingly. A single convincingly enhanced PS1 game is a real result.

## Actions

1. Add **normal map generation** to the roadmap, ahead of mesh replacement.
2. Add **height/parallax mapping** as the tractable 3D middle ground.
3. Add a **per-game profile system** keyed by disc serial — a prerequisite for
   nearly everything else.
4. Constrain the cache/pack format to carry **no PS1-derived data**.
5. Add an **audio enhancement** track.
6. Promote **save states + rewind** from "nice to have" to real milestones.
7. Record the **auto-vs-handdrawn quality bet** as the project's central
   hypothesis and primary risk.

## Sources

- [SUPER ZSNES official site](https://www.zsnes.com/) (content supplied by owner; site returns 403 to automated fetches)
- [GIGAZINE — SUPER ZSNES revival](https://gigazine.net/gsc_news/en/20260428-super-zsnes/)
- [Engadget — the sequel to ZSNES](https://www.engadget.com/gaming/nintendo/the-sequel-to-the-iconic-emulator-zsnes-is-called-super-zsnes-of-course-135203417.html)
- [80.lv — ZSNES returns with modern GPU rewrite](https://80.lv/articles/new-gpu-powered-snes-emulator-released)
- [Emulation General Wiki — SUPER ZSNES](https://emulation.gametechwiki.com/index.php/SUPER_ZSNES)
