#!/usr/bin/env python3
"""Default-gate boot of one local title: two enhancement-off runs, two on.

Skips (exit 0) when BIOS or discs are missing. A present disc that misses a
golden, or an on-run that matches the golden / disagrees with itself, fails.
"""
from __future__ import annotations

import io
import re
import sys
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
import boot_local  # noqa: E402


TITLE_HASH_RE = re.compile(r"hash=([0-9a-f]{16})")


def pick_game(spec: dict) -> dict | None:
    for gid in ("SCUS-942.54", "SLUS-007.26"):
        for game in spec["games"]:
            if game["id"] == gid and (ROOT / game["cue"]).is_file():
                return game
    for game in spec["games"]:
        if (ROOT / game["cue"]).is_file():
            return game
    return None


def title_expect(game: dict) -> dict:
    return max(game["expect"], key=lambda e: e["frame"])


def main() -> int:
    spec = boot_local.load_goldens()
    if not boot_local.QA_BIN.is_file():
        print("ARMSX_BOOT_SEE failed reason=no-qa-binary (run: make qa)", file=sys.stderr)
        return 1

    bios = boot_local.find_bios(spec, None)
    if bios is None:
        print("ARMSX_BOOT_SEE skip reason=no-bios")
        return 0

    game = pick_game(spec)
    if game is None:
        print("ARMSX_BOOT_SEE skip reason=no-discs")
        return 0

    golden = title_expect(game)["hash"]
    print(f"ARMSX_BOOT_SEE begin id={game['id']} golden={golden}", flush=True)

    off_hashes: list[str] = []
    for n in (1, 2):
        print(f"ARMSX_BOOT_SEE run=off-{n}", flush=True)
        rc = boot_local.run_game(bios, game, None, enhance=False)
        if rc:
            print(f"ARMSX_BOOT_SEE failed run=off-{n}", file=sys.stderr)
            return 1
        off_hashes.append(golden)

    cache = ROOT / "build" / "tests" / "see-boot-cache"
    on_hashes: list[str] = []
    for n in (1, 2):
        print(f"ARMSX_BOOT_SEE run=on-{n}", flush=True)
        buf = io.StringIO()
        with redirect_stdout(buf):
            rc = boot_local.run_game(
                bios, game, None, enhance=True, enhance_dir=cache, expect_diff=True
            )
        text = buf.getvalue()
        sys.stdout.write(text)
        if rc:
            print(f"ARMSX_BOOT_SEE failed run=on-{n}", file=sys.stderr)
            return 1
        title_frame = title_expect(game)["frame"]
        found = None
        for line in text.splitlines():
            if f"frame={title_frame}" in line and "differ=golden" in line:
                m = TITLE_HASH_RE.search(line)
                if m:
                    found = m.group(1)
        if not found:
            print("ARMSX_BOOT_SEE failed reason=no-on-title-hash", file=sys.stderr)
            return 1
        on_hashes.append(found)

    if off_hashes[0] != off_hashes[1] or off_hashes[0] != golden:
        print("ARMSX_BOOT_SEE failed reason=off-hash-mismatch", file=sys.stderr)
        return 1
    if on_hashes[0] != on_hashes[1]:
        print(
            f"ARMSX_BOOT_SEE failed reason=on-runs-disagree a={on_hashes[0]} b={on_hashes[1]}",
            file=sys.stderr,
        )
        return 1
    if on_hashes[0] == golden:
        print("ARMSX_BOOT_SEE failed reason=on-matched-golden", file=sys.stderr)
        return 1

    print(
        f"ARMSX_BOOT_SEE passed id={game['id']} off={golden} on={on_hashes[0]} runs=2+2",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
