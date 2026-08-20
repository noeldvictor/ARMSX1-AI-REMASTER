---
date: 2026-08-20 16:19
type: decision
status: active
tags: [architecture, pipeline, cache, editing, adr]
---

# ADR-004: AI first pass with human touch-up

Decision taken 2026-08-20 by the repository owner.

## Context

ADR-003 recorded the project's central risk: SUPER ZSNES's authors rejected
automatic upscaling as insufficient and hand-draw their enhancements instead.
The project was betting AI closes that gap, with no fallback if it does not.

The owner proposed: **AI does a first pass, humans touch up, and the original
stays viewable for reference.**

## Decision

Adopt a human-in-the-loop pipeline. This is now the core enhancement workflow,
not an optional extra.

### Why this resolves the central risk

It converts a binary bet into a spectrum:

- **AI at 100%** — nothing to do. Unlikely.
- **AI at 70%** — still a large win. A human fixes the 30% that bothers them.
- **AI at 20%** — the tool degrades gracefully into SUPER ZSNES's model, except
  the human starts from a draft rather than a blank canvas.

There is no quality level at which the project produces nothing. That was not
true before this ADR.

It also targets the known failure mode directly. AI upscalers reliably mangle
**text, fonts, logos, and faces** — precisely the things a human spots in
seconds and fixes quickly.

### D1 — Editing happens in the user's own image editor

Assets are PNGs on disk. The user edits them in Photoshop, Krita, Aseprite, GIMP
— whatever they already own — and the emulator **hot-reloads on save**, live,
while the game runs.

**We build a file watcher and a reload path. We do not build a paint program.**

Rejected: an in-emulator ImGui overlay editor. It means writing an image editor
from scratch that would be worse than tools the user already has, for far more
effort. A lightweight in-game quick-fix panel (revert / re-roll / brightness)
may be added later, but it is explicitly **not** a paint program.

### D2 — Auto-apply everything

Every AI result goes live immediately, with no approval gate. The game looks
enhanced as you play, which is the whole premise.

**Accepted consequence:** garbled fonts, logos, and faces *will* appear in-game
until noticed and fixed. This is a deliberate trade for immediacy and pipeline
simplicity. The review queue is therefore a **cleanup** queue, not a gate.

Rejected: approval-gated application (contradicts "remaster while playing" — it
becomes a prep step). Confidence-based gating was offered and declined in favour
of simplicity; it remains a viable later addition if mangled text proves
annoying in practice.

### D3 — Two comparison surfaces

**Hotkey A/B, whole screen.** Hold a key, the entire frame reverts to original;
release, enhanced returns. Cheap to implement — it bypasses the replacement
lookup. Serves as QA, as the demo, and as the fastest way to answer "did that
actually get better?"

**Side-by-side review queue.** A dedicated screen showing original / AI / user
version for one asset, with approve, reject, re-roll, and open-in-editor
actions. Triage outside of gameplay.

Rejected for now: split-screen wipe (nice for screenshots, redundant with the
hotkey) and per-asset highlight tinting (genuinely useful as a coverage view,
worth revisiting once there is coverage to view).

## Asset state machine

```
                    ┌──────────────┐
                    │   original   │  no AI result yet
                    └──────┬───────┘
                           │ worker produces draft
                           ▼
                    ┌──────────────┐
                    │      ai      │  auto-applied, live
                    └──┬────────┬──┘
              user edits│        │user rejects
                        ▼        ▼
             ┌──────────────┐  ┌──────────────┐
             │    edited    │  │   reverted   │
             │ user.png wins│  │ forced orig  │
             └──────────────┘  └──────────────┘
```

**`reverted` is load-bearing.** With auto-apply, a rejected asset would be
re-enhanced by the worker on next sighting forever. `reverted` and `edited` both
**lock** the asset against further AI passes unless the user explicitly re-rolls.

Resolution order at draw time: `user.png` → `ai.png` → original VRAM.

## Cache layout

```
cache/<GAMEID>/                       GAMEID = disc serial, e.g. SLUS-00594
  manifest.json                       hash → state, provenance, timestamps
  <hash>/
    orig.png                          dumped original — reference ONLY
    ai.png                            AI draft
    user.png                          human edit; wins if present
```

`manifest.json` records per asset: state, AI model and version, generation
timestamp, edit timestamp, and re-roll count. Provenance is needed so packs can
be merged, so re-rolls can target a newer model, and so a future session can
tell what has actually been reviewed.

### Pack export must strip originals

`orig.png` is **PS1-derived data**. ADR-003 requires shipped packs to contain no
ROM or copyrighted data.

**The local working cache keeps `orig.png` — the editor needs it as reference.
The pack export step deletes it.** A pack ships hashes plus generated assets
only. This distinction is easy to get wrong and would make packs unshippable, so
it is a format constraint from day one, not a later filter.

## Hot reload requirements

- Watch `cache/<GAMEID>/` recursively. inotify on Linux; polling fallback.
- **Debounce.** Image editors write in several syscalls; a naive watcher fires
  mid-write and loads a truncated PNG.
- Reload must not stall the render thread — decode off-thread, swap on the
  frame boundary.
- A malformed or partially-written PNG must **keep the previous texture**, never
  crash and never show garbage.

## Consequences

- The editor is the user's own; our surface is a watcher, a queue, and a hotkey.
- Milestone 7's AI worker gains a state machine and a lock concept.
- The review queue needs UI in the FSUI shell.
- The cache format is now a shipping artifact with export rules, and needs
  versioning from the start.
- Mangled text will be visible in-game until fixed. Accepted.

## Roadmap additions

| # | Deliverable |
| --- | --- |
| 5b | Cache format, manifest, asset state machine |
| 6e | Hotkey A/B compare (whole-frame original toggle) |
| 7c | File watcher + hot reload |
| 7d | Review queue UI (original / AI / user, approve / reject / re-roll / open) |
| 7e | Pack export — strips `orig.png`, writes provenance |
