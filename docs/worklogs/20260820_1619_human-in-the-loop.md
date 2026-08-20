---
date: 2026-08-20 16:19
type: worklog
status: complete
tags: [pipeline, cache, editing, adr, milestone-0]
---

# Human-in-the-loop pipeline design

## Goal

Act on the owner's proposal: AI does a first pass, humans touch up, original
stays viewable.

## Done

- `docs/decisions/20260820_1619_human-in-the-loop-pipeline.md` (ADR-004).
- `README.md` and `CLAUDE.md` updated: pipeline diagram, resolution order, hot
  reload traps, pack export rule, roadmap entries.

## Decisions recorded

| | Choice | Rejected |
| --- | --- | --- |
| Edit surface | User's own image editor + hot reload | In-emulator paint program |
| Trust model | Auto-apply everything | Approval gate; confidence gating |
| Compare | Hotkey whole-frame A/B **and** side-by-side review queue | Split wipe; per-asset tinting |

## Why this matters

ADR-003 left the project on a binary bet: AI is good enough, or there is no
product. ADR-004 removes that.

- AI at 70% → human fixes 30%, still a win.
- AI at 20% → degrades into SUPER ZSNES's model, but starting from a draft
  rather than a blank canvas.

**There is no quality level at which the project produces nothing.** That was
not true this morning.

It also targets the known failure mode: AI upscalers mangle text, fonts, logos
and faces — exactly what a human spots in seconds.

## Design details worth not rediscovering

1. **`reverted` / `edited` must LOCK an asset.** With auto-apply, a rejected
   asset would otherwise be re-enhanced by the worker on every sighting,
   forever. This is load-bearing, not a nicety.
2. **Resolution order:** `user.png` → `ai.png` → original VRAM.
3. **Pack export must strip `orig.png`.** It is PS1-derived data, and ADR-003
   forbids it in shipped packs. But the *local* cache must keep it, because the
   editor needs it as reference. Getting this backwards makes packs
   unshippable. Constraint from day one, not a later filter.
4. **Debounce the file watcher.** Image editors write in several syscalls; a
   naive watcher fires mid-write and loads a truncated PNG. A malformed or
   partial PNG must keep the previous texture — never crash, never show garbage.
5. **Hotkey A/B is cheap** — it bypasses the replacement lookup rather than
   re-rendering anything.

## Accepted trade

Auto-apply means **garbled fonts and logos will appear in-game** until noticed
and fixed. The owner chose this deliberately for immediacy and pipeline
simplicity. Confidence-based gating was offered and declined; it stays viable if
mangled text proves annoying in practice.

## Verified

`python3 tests/run_validation.py` → exit 0, all 8 cases pass. Docs only this
session; no code changed.

## Broken / Known issues

- Unchanged: `bin/armsx` **NOT VERIFIED at runtime**. No BIOS, no discs.
- No code written for any of this. ADR-004 is design only. Milestones 5b, 6e,
  7c, 7d, 7e are all unstarted.

## Next

Unchanged: **milestone 1**, Vulkan backend skeleton. ADR-004 changes nothing
before milestone 5 — but the cache format work at 5b now has a specification to
build against rather than being invented later.
