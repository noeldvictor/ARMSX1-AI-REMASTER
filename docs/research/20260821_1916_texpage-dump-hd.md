---
date: 2026-08-21 19:16
type: research
status: complete
tags: [see, textures, cache]
---

# Texture-page dump and hash-tagged HD without a replay

## What was missing

The 2D SEE path only hashed CPU-to-VRAM uploads and the presented
framebuffer. 3D (and most 2D sprites) live in texture pages and are
sampled through a CLUT at draw time. Those never become a single
LoadImage, so they never got an `orig.png`. Replaying the game was the
only way to "see" a surface again for generation.

## What we hash

Not the decoded RGB. The cache key is FNV-1a over page coordinates,
depth, texture window, the raw VRAM page words, and the CLUT words.
Same VRAM+palette → same folder. Decoded 256×256 RGB is `orig.png` for
the editor; `meta.json` records `kind=texpage` plus tpx/tpy/clut/depth.

## How HD is tagged without a replay

1. Play (even with enhance off). Dumps write `orig.png` once per hash.
2. `see_enhance_cache()` walks `cache/<serial>/*/orig.png` and writes
   `generated.png` for anything not locked. No GPU, no disc.
3. Or drop `user.png` next to `orig.png`. That folder name **is** the
   tag.
4. Next boot: `see_on_texture_use` binds user/generated; `see_replace_texel`
   samples HD at 8-bit UV. Texture-page VRAM is not written.

Enhance-off Legaia title boot (841 frames) dumped **53** `orig.png`
(26 with `meta.json` texpages) and still matched goldens
`de6e29fed86fac80` / `1c859cc02ebe806e`.

## What this is not

True in-game 3D HD at internal resolution still needs the Vulkan
rasterizer (milestone 6b). At native res, HD sampling only changes the
5-bit colour written into the framebuffer when enhance is on.
Enhance-off never installs `texel_cb`, so the oracle is unchanged.
