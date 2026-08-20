---
date: 2026-08-20 14:50
type: decision
status: active
tags: [architecture, vulkan, pgxp, textures, adr]
---

# ADR-001: Remaster architecture

Decisions taken 2026-08-20 by the repository owner. Supersedes nothing.

## Context

`ARMSX1-AI-REMASTER` exists to answer one question: **can a PS1 game be made to
look remastered while you are playing it**, driven mostly by AI-authored code
and AI-generated assets?

The baseline is measured in
[`20260820_1438_duckstation-feature-gap.md`](../research/20260820_1438_duckstation-feature-gap.md).
Short version: the fork has an accurate software rasterizer, a complete GTE, and
none of the enhancement infrastructure.

## Decisions

### D1 — Full DuckStation-class GPU rewrite

A new Vulkan renderer backend, PGXP geometry correction, internal resolution
scaling, then texture replacement on top.

Rejected: frame-level post-processing only (ceiling is "smoothed PS1", cannot
produce real HD detail, mangles HUD and text); texture-tap on the software path
(nowhere to put a high-res texture — native-res 16-bit VRAM is the wrong target).

Accepted cost: months, not weeks. This is the honest price of the goal.

### D2 — Vulkan, desktop and Android only

Reference deployment device is the **AYN Thor** — Snapdragon 8 Gen 2 / Adreno 740
(Lite variant: Snapdragon 865 / Adreno 650), 8–16 GB RAM, dual OLED, Android.
Desktop Linux is the development target.

Baseline: **Vulkan 1.1 core**, with 1.3 features used opportunistically behind
capability checks. Adreno 740 reports 1.3; Adreno 650 is 1.1 on older drivers.
Targeting 1.1 keeps the Lite variant alive at near-zero cost.

Rejected: GLES 3.0 / WebGL2 (would keep more ports alive, but gives up compute
shaders — and compute is what makes live texture upscaling and async streaming
tractable); GL 3.3 desktop-first (defers the Android problem, and Android *is*
the deployment target, not an afterthought).

Explicitly out of scope: WebAssembly, PSVita, UWP, iOS, macOS.

### D3 — Desktop Linux + Android only; other ports frozen, not deleted

`uwp/`, `psvita/`, `web/`, `ios/` stay in the tree on the existing software
path. They will not receive remaster features and will not be verified. Nothing
is deleted — reversibility is worth more than a smaller tree.

Rejected: deleting unused ports (irreversible without archaeology, saves little).

### D4 — Live async enhancement with a progressive cache

First sighting of a texture renders native. A background worker enhances it; the
HD version swaps in a moment later and is cached to disk permanently. Subsequent
sightings — and every later session — are instant cache hits.

This is the literal reading of "auto-remastered while playing", and it is what
was asked for. The costs are real and accepted:

- Visible pop-in when a texture upgrades mid-scene.
- Unbounded API spend during a session unless a budget cap is enforced.
- Nondeterminism — two playthroughs will not look identical.

Mitigations, required in the implementation:
- Hard per-session API budget, enforced in code, with the cap surfaced in the UI.
- Cheap algorithmic path (xBRZ / edge-directed) runs **first** and always; AI
  enhancement is an upgrade pass on top, never the only path.
- The cache is a first-class, inspectable, shippable artifact. Once a game is
  fully explored its cache *is* an HD texture pack, and can be shared as one.

Rejected: offline pre-bake only (safer and cheaper, but it is not the stated
goal — "while playing" was the requirement).

### D5 — Three tracks; mesh replacement is research

| Track | Status | Ships? |
| --- | --- | --- |
| 1. HD textures | Feature | Yes |
| 2. PGXP geometry | Feature | Yes |
| 3. Mesh replacement (Tripo) | Research | Only if it works |

Tracks 1 and 2 never block on track 3.

The hard part of track 3 is **not** generating a mesh — Tripo does that. It is
identifying *which* mesh, frame to frame, from a stream of GTE-transformed
triangles that carry no object identity. Open sub-problems, all unsolved here:
vertex-set fingerprinting stable under animation; rigging a generated mesh to
original animation data; scale and pivot matching; per-frame identification cost
inside a 16 ms budget.

Track 3 gets its own research docs and is documented **even if it fails**. A
well-evidenced negative result is a real deliverable.

### D6 — Verification: headless gate plus real discs

- **Gate, every change:** `python3 tests/run_validation.py`, extended with
  Vulkan-vs-software parity via `tests/gpu_renderer_parity.c`. Scripted GPU
  command streams, golden hashes, no BIOS or disc needed.
- **Spot check, visual work:** boot real games, capture frames, compare.

The software rasterizer is the **parity oracle**. It is never deleted and never
"replaced" — the Vulkan path is validated against it.

Blocked on: BIOS path and 2–3 disc images. Until those are provided, only the
headless half of the gate can run, and any claim about visual quality is
unverified and must be labelled as such.

## Consequences

- The software renderer stays forever. It is the reference, not legacy code.
- `USE_HARDWARE` is a misleading name for a shim and needs renaming before a
  real hardware path lands, or the confusion becomes permanent.
- Save states (`psx/psx.c:11-21`, currently `exit(1)`) become a real dependency
  and must be implemented.
- The cache format is a shipping artifact and deserves versioning from day one.

## Milestones

| # | Deliverable | Gate |
| --- | --- | --- |
| 0 | Repo bootstrap, tracking, CI removal | — |
| 1 | Vulkan backend skeleton, VRAM blit parity with software | Parity hashes |
| 2 | Vulkan rasterization of all primitive types | Parity hashes |
| 3 | Internal resolution scaling | Visual + parity at 1× |
| 4 | PGXP vertex pipeline (GTE tap) | Wobble gone, no regressions |
| 5 | Texture hashing, dumping, cache | Dumps match VRAM |
| 6 | Replacement lookup + hi-res sampling | HD pack renders |
| 7 | Live async enhancement worker | Budget cap honoured |
| 8 | Mesh fingerprinting research | Written up either way |
