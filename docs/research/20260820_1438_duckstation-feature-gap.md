---
date: 2026-08-20 14:38
type: research
status: complete
tags: [gpu, duckstation, feature-gap, pgxp, baseline]
---

# ARMSX1 vs DuckStation: enhancement feature gap

## Goal

Establish, from the source rather than from the README, exactly which
enhancement features ARMSX1 lacks relative to DuckStation, and which of those
are prerequisites for the AI remaster work.

## Method

Direct read of `psx/dev/gpu.c` (2208 lines), `psx/dev/gpu.h`, `psx/cpu.c`,
`frontend/gpu_hw.c`, `frontend/config.h`, and `psx/psx.c`. Every claim below
cites the line that supports it. No claim is inferred from documentation.

## The single most important finding

`frontend/gpu_hw.c` is **not a hardware renderer**. It is a compatibility shim.
The file's own comment says so, and its one substantive function is:

```c
void gpu_hw_render_triangle(psx_gpu_t* gpu, vertex_t v0, vertex_t v1, vertex_t v2, poly_data_t data, int edge) {
    gpu_render_triangle(gpu, v0, v1, v2, data, edge);   // frontend/gpu_hw.c:64
}
```

It forwards straight back into the software rasterizer. `USE_HARDWARE` is
defined unconditionally in `Makefile:181`, so the name is actively misleading:
builds "with hardware" are still 100% software. The README's "SDL accelerated"
mode is a *presentation* path — it uploads dirty VRAM scanlines to an SDL
texture (`README.md`) and explicitly does not replace PS1 rasterization.

**There is no GPU-accelerated rasterization anywhere in this codebase.**

## The precision ceiling

This is the structural blocker, and it is worth understanding precisely.

```c
typedef struct {
    int16_t x, y;      // screen-space, integer, already projected
    uint32_t c;
    uint8_t tx, ty;    // texcoords, 8-bit, integer
} vertex_t;            // psx/dev/gpu.h:73-77
```

By the time a vertex reaches the rasterizer it is an integer 2D screen
coordinate. There is no Z, no W, no float. This is *faithful* — it is what real
PS1 hardware receives — and it is exactly why PS1 games wobble: the GTE rounds
transformed vertices to integers before handing them to the GPU, and the GPU
has no depth information to do perspective-correct texturing.

Consequences that follow directly from this struct:

- **Polygon wobble is baked in.** Vertices snap to integer pixels every frame.
- **Texture warping is baked in.** `gpu_render_textured_triangle`
  (`psx/dev/gpu.c:911`) interpolates U/V affinely, because it has no W to
  divide by. This is the classic swimming-texture artifact.
- **No depth buffer exists.** Draw order is the only sorting.
- **Texture coordinates are 8-bit** — a 256×256 window into VRAM.

You cannot fix any of this downstream. PGXP works by tapping the GTE *before*
the rounding happens (`psx_gte_i_rtps` / `rtpt`, `psx/cpu.c:92,110`) and
carrying the full-precision floats forward to the rasterizer. That requires
both a new vertex path and a renderer that can consume it.

## Gap table

Verified against source. "Absent" means no implementation exists, not that it is
partial.

### Blocking for the remaster

| Feature | DuckStation | ARMSX1 | Evidence |
| --- | --- | --- | --- |
| Hardware renderer | GL / Vulkan / D3D11/12 / Metal | **Absent** | `frontend/gpu_hw.c:64` forwards to software |
| Internal resolution scaling | 1×–32× | **Absent** | Rasterizer writes native-res into 16-bit VRAM |
| PGXP geometry correction | Yes | **Absent** | No hits for `pgxp` anywhere in tree |
| PGXP perspective-correct texturing | Yes | **Absent** | Affine interpolation, `gpu.c:911` |
| Texture replacement / HD packs | Yes, hash-keyed | **Absent** | No texture cache, no hashing |
| Texture dumping | Yes | **Absent** | Same |
| Depth buffer | Yes (HW path) | **Absent** | No Z in `vertex_t` |
| 24-bit colour output | Yes | **Absent** | 16-bit BGR555 throughout, `gpu_to_bgr555:24` |

### Non-blocking but notable

| Feature | DuckStation | ARMSX1 | Evidence |
| --- | --- | --- | --- |
| Save states | Yes, with rewind | **Stubbed — calls `exit(1)`** | `psx/psx.c:11-21` |
| Rewind / runahead | Yes | **Absent** | Depends on save states |
| Widescreen hack (FOV) | Yes, GTE-level | **Absent** | `display_aspect` (`config.h:26`) only stretches output |
| CPU recompiler | Yes (JIT) | **Absent by design** | `cached` + `interpreter` only; no codegen, no exec memory |
| CPU overclocking | Yes | **Absent** | `PSX_CPU_CPS` fixed, `psx/cpu.h:9` |
| Cheats / GameShark | Yes | **Absent** | No matches in tree |
| RetroAchievements | Yes | **Absent** | Listed in `TODO` |
| Multitap | Yes | **Absent** | `joy_slot[2]`, `psx/dev/pad.h:111` |
| Rumble | Yes | **Absent** | No matches |
| Post-processing shaders | Yes | **Absent** | — |
| Texture filtering | xBR/JINC2/etc. | **Partial** | Software bilinear exists, `gpu.c:207` |

### Already present — do not rebuild

Worth stating explicitly so no session wastes effort here.

- **Full GTE.** All ops implemented: `rtps`, `rtpt`, `nclip`, `mvmva`, `ncds`,
  `ncdt`, `dpcs`, `intpl`, `sqr`, `avsz3/4` and the rest (`psx/cpu.c:92-110`).
  This is the tap point for PGXP — it exists and it is complete.
- **Accurate software rasterizer** with 4×4 dithering (`gpu.c:17,333,880`),
  texture-window masking, semi-transparency, and 4/8/15-bit CLUT modes
  (`gpu_fetch_texel:175`). This is the parity oracle for the Vulkan renderer.
- **Software bilinear texel fetch** (`gpu_fetch_texel_bilinear:207`) — already
  handles the CLUT-aware filtering problem, and correctly returns early on
  transparent texels.
- **Analog controller support.** The `// To-do: Implement analog mode` comment at
  `psx/input/sda.c:124` is **stale** — the function below it does store all four
  ADC axes, and `sda.c:44,86` transmit them in analog mode. Do not "fix" this
  based on the comment; verify behaviour first.
- **Broad disc support**: BIN/CUE, ISO, CHD with subchannel data, ZIP.
- **A deterministic test harness**: `tests/run_validation.py`, including
  `gpu_renderer_parity.c` — the natural place to hang Vulkan parity tests.

## What this means for the remaster

The dependency chain is strict and there is no way to shortcut it:

```
Vulkan renderer  ──►  internal res scaling  ──►  HD texture replacement
      │                                                    ▲
      └──►  PGXP (GTE tap) ──►  perspective-correct UVs ────┘
                    │
                    └──►  stable geometry, no wobble
```

Texture replacement is **not** an independent feature that can be bolted onto
the software path. It needs somewhere to put a 4096×4096 replacement texture,
and 16-bit native-res VRAM is not that place. The Vulkan renderer is the
prerequisite for everything else, which is why it is milestone one.

## Risks identified

1. **The software rasterizer is the compatibility record.** `compat.txt`
   documents dozens of game-specific fixes ("Fixes Mortal Kombat II, Bubble
   Bobble, Driver 1 & 2" — `gpu.c:1109`). A Vulkan renderer that does not
   reproduce these behaviours will regress games that currently work. The
   software path must stay as the reference and the parity oracle. Deleting or
   "replacing" it would destroy years of accumulated fixes.
2. **Save states are a hard dependency for the AI cache work.** Reproducing "the
   exact moment texture X first appears" for offline baking effectively requires
   them, and they currently `exit(1)`.
3. **Live async texture enhancement on Adreno needs a transfer queue.** Uploading
   a large replacement texture on the graphics queue mid-frame will stall. This
   is a design constraint on the Vulkan backend from day one, not a later
   optimisation.

## Sources

- [DuckStation feature list](https://github.com/stenzek/duckstation)
- [PGXP — original project](https://github.com/iCatButler/pcsxr-pgxp)
- All ARMSX1 claims: direct source read, cited inline above.
