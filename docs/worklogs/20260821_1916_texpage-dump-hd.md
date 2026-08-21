---
date: 2026-08-21 19:16
type: worklog
status: complete
tags: [see, textures, milestone-5]
---

# Texture dumper and hash-tagged HD replacements

## Goal
Dump textures when they are sampled, tag HD by the same hash, and
generate HD from the cache so a second playthrough is not required.

## Done
- `see_on_texture_use` decodes a 256×256 CLUT page, writes `orig.png` +
  `meta.json` once, appends `dumps.jsonl`. Does not write VRAM.
- `see_enhance_cache` walks dumped `orig.png` and writes `generated.png`
  for unlocked assets (no replay).
- `see_replace_texel` samples bound `user.png` / `generated.png` at 8-bit
  UV. GPU `texel_cb` is installed only when enhance is on.
- Dump observers (`vram_write`, `texture_use`, present ingest) run even
  with enhance off. Drop `user.png` in `cache/<serial>/<hash>/` to tag HD.
- QA always attaches dump callbacks; texel replacement only with
  `--enhance`.

## Verified
Executed; output is real.

```
make test-see
SEE_REPLACEMENT passed case=texpage-dump-and-hd
SEE_REPLACEMENT passed case=enhance-cache-no-replay
SEE_REPLACEMENT all cases passed
```

Repeat `make test-see` → still pass (cache wipe in the texpage case).

```
python3 tests/boot_local.py --game SCUS-942.54
ARMSX_BOOT serial=SCUS-942.54 enhance=False applied=False assets=53
ARMSX_BOOT passed ... frame=240 640x480 hash=de6e29fed86fac80
ARMSX_BOOT passed ... frame=840 320x228 hash=1c859cc02ebe806e
```

53 `orig.png` under `cache/SCUS-942.54/` (26 texpage `meta.json`),
goldens unchanged. 0 `generated.png` because enhance was off — run
`see_enhance_cache` (covered by the unit test) to HD them without
playing.

`python3 tests/run_validation.py --case source --case see --case qa
--case vk --case cpu` after the wipe fix: see/qa/vk/cpu/source pass.
(An earlier see run failed `texpage-not-dumped` on leftover cache;
fixed.)

## Broken / Known issues
- Headed frontend still has no SEE hooks (no SDL2 on this host).
- Native-res HD texel replace cannot add geometric detail; Vulkan
  rasterization is still required for true 3D HD.

## Open questions
None for dump + hash tag.

## Next
Wire the same dump/replace callbacks into the SDL frontend when SDL2
is available, or run `see_enhance_cache` over the 53 Legaia dumps and
spot-check an enhance-on boot.
