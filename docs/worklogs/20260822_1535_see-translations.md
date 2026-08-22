---
date: 2026-08-22 15:35
type: worklog
status: complete
tags: [see, textures, translations]
---

# SEE translation overlay and dump catalog

## Goal
Make translations as easy as HD: dump once, paint over orig.png, tag by
hash, no replay.

## Done
- Draw order is now `xlat-<lang>.png` → `user.png` → `generated.png` →
  original (`enhance/see.c` `see_pick_replacement`).
- `see_set_language("en")` sanitizes to `[a-z0-9]`. Empty language
  ignores xlat files (HD path unchanged).
- Any `xlat-*.png` locks the asset against further AI `generated.png`
  passes, same as `user.png` / `reverted`.
- `see_write_catalog` writes `cache/<serial>/catalog.html`: orig.png
  contact sheet plus “save xlat-en.png here”.
- Pack export copies `xlat-*.png` and records `"xlat": true`; still
  strips `orig.png`.
- QA `--lang=CODE`; catalog written at end of a run.

## Verified
Executed; output is real.

```
make test-see
SEE_REPLACEMENT passed case=xlat-en-catalog
SEE_REPLACEMENT all cases passed
SEE_PACK passed serial=SCUS-942.54 hash=e7b7fa0cc9267a59 stripped=orig.png xlat=en
```

```
python3 tests/boot_local.py --game SCUS-942.54
ARMSX_BOOT passed frame=240 640x480 hash=de6e29fed86fac80
ARMSX_BOOT passed frame=840 320x228 hash=1c859cc02ebe806e
```

Goldens unchanged (enhance off, dumps already present).

## Broken / Known issues
- Headed frontend still has no SEE / language hook.
- Translators still paint on the texture (PS1 text is pixels). There is
  no string table.

## Open questions
None for this slice.

## Next
Open `cache/SCUS-942.54/catalog.html` after a dump run and drop a
`xlat-en.png` on a UI texture, then boot with `--enhance --lang=en`.
