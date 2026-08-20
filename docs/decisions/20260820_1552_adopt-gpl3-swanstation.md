---
date: 2026-08-20 15:52
type: decision
status: active
tags: [licensing, gpl, swanstation, adr]
---

# ADR-002: Adopt GPL-3.0 and use SwanStation as the source

Decision taken 2026-08-20 by the repository owner.

## Context

Milestones 1–6 need a Vulkan renderer, PGXP, internal resolution scaling, and
texture replacement. Analysis in
[`20260820_1541_adaptable-prior-art.md`](../research/20260820_1541_adaptable-prior-art.md)
established that DuckStation itself is unusable (CC BY-NC-ND 4.0, NoDerivatives)
but that **SwanStation** — the GPL-3.0 hard fork taken from DuckStation's final
GPLv3 commit — has all four and is legitimately licensed.

## Decision

**Adopt GPL-3.0 for this project and derive from SwanStation.**

`LICENSE` has been changed from LGPL-3.0 to GPL-3.0. LGPLv3 §2 explicitly
permits conveying a covered work under GPLv3, so this relicense is permitted.

- Previous LGPL-3.0 text preserved at `LICENSES/PREVIOUS-LGPL3.txt`.
- Attribution and provenance recorded at `LICENSES/SWANSTATION-GPL3.txt`.
- Upstream `psxe` MIT terms unchanged (`LICENSES/UPSTREAM-MIT.txt`); MIT is
  permissive and absorbs into GPL without conflict.

### Source of truth for provenance

Reference clone at `reference/swanstation`, **gitignored** (116 MB, not part of
this project). Pinned at commit `7f69c19` (2026-08-11).

`libretro/swanstation` is actively maintained — its most recent commit is nine
days old as of this ADR, not an abandoned snapshot.

### What is explicitly forbidden

**No code from post-relicense DuckStation, ever.** It is CC BY-NC-ND 4.0 and
NoDerivatives means what it says. Only the GPL-3.0 SwanStation lineage is used.
An agent that copies from `stenzek/duckstation` creates a licence violation that
cannot be cured by deleting it later.

**No GPLv2-only code.** GPLv2-only is incompatible with GPLv3. Beetle PSX and
PCSX-R require per-file header verification before anything is taken. PPSSPP and
Dolphin are GPLv2+ and therefore safe.

## Consequences accepted

- The project is now **GPL-3.0**. This is irreversible in practice.
- Distributing binaries obliges offering full corresponding source.
- Any future closed-source or proprietary plan for this codebase is foreclosed.
  Commercial *sale* remains permitted — GPL has never prohibited it — but the
  source must accompany it.
- Anyone may fork this work, and that cannot be prevented.
- Every derived file must carry attribution headers naming SwanStation and
  DuckStation's original authors.

The owner was advised of each of these and accepted them.

## Rejected alternatives

- **Stay LGPL-3.0, write everything clean-room.** Months of avoidable work
  reinventing a solved Vulkan/PGXP design, with no reference implementation to
  check behaviour against. Rejected as pointless for an experimental fork.
- **Use Beetle PSX instead.** Also has a Vulkan renderer and the original PGXP,
  but its Mednafen lineage is GPLv2 and per-file v2-only headers would need
  auditing before anything could be merged. SwanStation's GPL-3.0 is clean.

## Not legal advice

This records an owner decision, not a legal opinion. Anything commercial should
be reviewed by a lawyer before distribution.
