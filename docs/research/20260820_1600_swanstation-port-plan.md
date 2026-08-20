---
date: 2026-08-20 16:00
type: research
status: complete
tags: [swanstation, vulkan, pgxp, texture-replacement, port-plan]
---

# SwanStation port plan

## Goal

Now that GPL-3.0 is adopted (ADR-002), determine concretely what to take from
SwanStation, how big each piece is, and where it lands in ARMSX1.

## Reference

`reference/swanstation` @ `7f69c19` (2026-08-11), gitignored. GPL-3.0.
**Actively maintained — last commit nine days before this doc.**

## Licence audit — per file, not per badge

Verified by reading actual file headers, as required by ADR-002.

| File | Header | Compatible? |
| --- | --- | --- |
| `src/core/pgxp.cpp` / `.h` | GPLv2 **"or any later version"** — iCatButler, 2016, via Beetle PSX | **Yes.** The "or later" clause permits GPLv3 |
| `src/core/gpu_hw*.cpp` / `.h` | No per-file header; repo GPL-3.0 | Yes |
| `src/core/texture_replacements.*` | No per-file header; repo GPL-3.0 | Yes |
| `src/common/vulkan/*` | No per-file header; repo GPL-3.0 | Yes |

The GPLv2-only trap flagged in
[`20260820_1541_adaptable-prior-art.md`](20260820_1541_adaptable-prior-art.md)
**does not bite here.** PGXP is v2+, not v2-only. Had it been v2-only it could
not have been used at all.

## What exists and how big it is

| Component | Files | Lines | Milestone |
| --- | --- | --- | --- |
| Vulkan helper layer | `src/common/vulkan/*` | 4,787 | 1 |
| HW renderer, shared | `gpu_hw.cpp` / `.h` | 2,128 | 1–3 |
| Vulkan backend | `gpu_hw_vulkan.cpp` / `.h` | 4,106 | 1–3 |
| Shader generator | `gpu_hw_shadergen.cpp` / `.h` | 1,879 | 1–3 |
| PGXP | `pgxp.cpp` / `.h` | 1,740 | 4 |
| Texture replacement | `texture_replacements.cpp` / `.h` | 325 | 5–6 |
| **Total** | | **~14,965** | |

Roughly fifteen thousand lines of solved problem. That is the value of ADR-002.

## Finding 1 — texture replacement is in the snapshot, but it is VRAM-write only

This is the most consequential discovery for the remaster goal.

```cpp
enum class ReplacmentType
{
  VRAMWrite          // ← the only member
};

const TextureReplacementTexture* GetVRAMWriteReplacement(
    uint32_t width, uint32_t height, const void* pixels);

TextureReplacementHash GetVRAMWriteHash(
    uint32_t width, uint32_t height, const void* pixels) const;
```

It hashes an **entire VRAM upload** (128-bit hash, `low`/`high`) and swaps in an
RGBA8 image. That is the whole mechanism.

**What this gives us for free:** 2D. Backgrounds, sprites, UI, fonts, title
screens, pre-rendered art — anything the game uploads to VRAM as a complete
image gets hashed and replaced. This is a large fraction of the visible surface
in most PS1 games and essentially all of the surface in 2D games.

**What it does not give us:** 3D model textures. Those live inside texture pages
and are sampled per-draw through a CLUT with 8-bit U/V. A VRAM-write hash cannot
address them — a single upload may pack dozens of unrelated textures, and the
game may never re-upload them.

DuckStation added per-draw texture-page replacement **later**, and that work is
post-relicense. It is CC BY-NC-ND and **must not be looked at for
implementation**. The 3D texture path is original work for this project.

This confirms, from the source rather than from intuition, the earlier
assessment: **2D is easy, 3D is the hard part.** The split is now precisely
located — it is exactly the `ReplacmentType` enum having one member.

## Finding 2 — PGXP is far more invasive than "tap the GTE"

`pgxp.h` exposes **44 CPU hook functions**, not a handful of GTE ones:

```c
void CPU_MFC2/MTC2/CFC2/CTC2(...);   // GTE register moves
void CPU_LWC2/SWC2(...);             // GTE memory access
void CPU_LW/LHx/SW/SH/...(...);      // ordinary loads and stores
bool GetPreciseVertex(uint32_t addr, uint32_t value, int x, int y, ...);
```

PGXP does not merely intercept the GTE. It maintains a **shadow value graph**
that tracks full-precision coordinates as they flow through CPU registers and
main memory, so that when an integer vertex finally reaches the GPU it can be
looked up by provenance (`addr`, `value`) and the original float recovered.

**Consequence for ARMSX1:** hooks are needed in `psx/cpu.c` across ordinary
load/store instructions, and **in both CPU engines** — `cached` and
`interpreter` — or the two engines will diverge and the `cpu` differential test
will start failing. The earlier note in
[`20260820_1438_duckstation-feature-gap.md`](20260820_1438_duckstation-feature-gap.md)
that "the GTE is the tap point" was correct but incomplete: the GTE is where
precision is *produced*, and the whole load/store path is where it must be
*preserved*.

Budget milestone 4 accordingly. It is not a small feature.

## Architecture gap

Porting is not copy-paste. The two codebases differ structurally:

| | SwanStation | ARMSX1 |
| --- | --- | --- |
| Language | C++17 throughout | C11 core, C++ frontend |
| Core structure | `GPUBackend` class hierarchy | Free functions on `psx_gpu_t` |
| VRAM ownership | Backend owns; native-res copy for readback | `gpu->vram` authoritative, always native |
| Frontend | libretro core | SDL2 + FSUI |
| Build | CMake | Makefile + `build.sh` |
| CPU | Dynarec + interpreter | `cached` + `interpreter`, **no codegen ever** |

`gpu_hw_vulkan.cpp` will not compile in this tree and would not be worth
debugging if it did. The port is a **deliberate reimplementation against a known
-good reference**, not a merge.

## What is worth taking, in priority order

1. **`src/common/vulkan/*` (4,787 lines)** — the least ARMSX1-specific and most
   mechanically portable piece. Device/swapchain selection, command buffer
   management, staging buffers, texture/streaming helpers. Port close to
   verbatim; this is milestone 1's foundation.

2. **`gpu_hw.cpp` design, not its code** — the genuinely hard-won parts, which
   cost weeks to rediscover independently:
   - VRAM-as-texture with native-res readback for VRAM-to-CPU operations
   - Mask-bit and semi-transparency pipeline state permutations
   - Keeping native-res VRAM coherent while rendering at higher internal res
   - `GetDownsampleMode(resolution_scale)` for correct downsampling

3. **`pgxp.cpp` (1,740 lines)** — port closely. The value-tracking design is
   subtle and reinventing it is a waste. Preserve iCatButler's GPLv2+ header.

4. **`texture_replacements.*` (325 lines)** — small, and its real value is the
   **hash and cache-file-format decisions**, which we should match so packs are
   interchangeable. Take it, then extend beyond `VRAMWrite`.

5. **`gpu_hw_shadergen.cpp`** — reference only. SwanStation generates shaders for
   four APIs; we need Vulkan/SPIR-V alone and should write a simpler generator.

## Not to be touched

- **Dynarec.** ADR-001 keeps the no-codegen property deliberately. Do not port
  `cpu_recompiler_*`.
- **libretro host interface.** Irrelevant to an SDL2 frontend.
- **D3D11/D3D12/OpenGL backends.** Vulkan only, per ADR-001.
- **Post-relicense DuckStation, in any form, for any reason.**

## Revised milestone estimates

| # | Deliverable | Change |
| --- | --- | --- |
| 1 | Vulkan skeleton, VRAM blit parity | **Easier** — port `common/vulkan` |
| 2 | Vulkan rasterization | **Much easier** — `gpu_hw` reference |
| 3 | Internal resolution scaling | **Much easier** — solved design |
| 4 | PGXP | **Harder than assumed** — 44 CPU hooks, both engines |
| 5 | Texture hash / dump / cache | **Easier** — proven hash + format |
| 6 | Replacement, 2D | **Much easier** — works out of the box |
| 6b | Replacement, 3D texture pages | **NEW, original work.** No usable prior art |
| 7 | Live async AI worker | Unchanged |
| 8 | Mesh fingerprinting | Unchanged — no prior art anywhere |

Milestone 6 splits. 6 (2D) is close to free; **6b (3D) is the real research
problem**, and it is where the AI-remaster ambition actually lives.

## Next action

Milestone 1. Port `src/common/vulkan/*` behind a renderer-backend interface,
targeting Vulkan 1.1 with 1.3 probing (ADR-001). Before writing the renderer,
extend `tests/gpu_renderer_parity.c` with a Vulkan-vs-software comparison so the
gate exists before the thing it gates.

Rename `USE_HARDWARE` in the same change — it currently guards a shim that
forwards to the software rasterizer (`frontend/gpu_hw.c:64`) and becomes
actively dangerous once a real hardware path exists.
