#!/usr/bin/env python3
"""Boot local BIOS+discs through armsx-qa and check golden frame hashes.

bios/ and roms/ are gitignored. If a game's cue (or the BIOS) is missing the
case is skipped, not failed — this must not break the gate on a machine that
has no media. A present disc whose hash does not match is a real failure.

Usage:
  python3 tests/boot_local.py
  python3 tests/boot_local.py --game SCUS-942.54
  python3 tests/boot_local.py --list
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GOLDENS = ROOT / "tests" / "qa" / "goldens.json"
QA_BIN = ROOT / "build" / "tools" / "armsx-qa"


def load_goldens() -> dict:
    return json.loads(GOLDENS.read_text())


def find_bios(spec: dict, override: str | None) -> Path | None:
    if override:
        p = Path(override)
        return p if p.is_file() else None
    env = os.environ.get("ARMSX_BIOS")
    if env:
        p = Path(env)
        if p.is_file():
            return p
    for rel in spec["bios_candidates"]:
        p = ROOT / rel
        if p.is_file() and p.stat().st_size == 512 * 1024:
            return p
    return None


def parse_json_events(stdout: str) -> list[dict]:
    events = []
    for line in stdout.splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            events.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return events


def run_game(
    bios: Path,
    game: dict,
    capture_dir: Path | None,
    enhance: bool = False,
    enhance_dir: Path | None = None,
    expect_diff: bool = False,
) -> int:
    cue = ROOT / game["cue"]
    script = ROOT / game["script"]
    if not cue.is_file():
        print(f"ARMSX_BOOT skip id={game['id']} reason=no-cue path={cue}", flush=True)
        return 0
    if not script.is_file():
        print(f"ARMSX_BOOT failed id={game['id']} reason=missing-script path={script}", file=sys.stderr)
        return 1

    max_frame = max(e["frame"] for e in game["expect"])
    frames = max_frame + 1

    cmd = [
        str(QA_BIN),
        f"--bios={bios}",
        f"--cdrom={cue}",
        f"--script={script}",
        f"--frames={frames}",
        "--json",
        "--quiet",
    ]
    if capture_dir:
        capture_dir.mkdir(parents=True, exist_ok=True)
        cmd.append(f"--capture-dir={capture_dir}")
    if enhance:
        cmd.append("--enhance")
        cmd.append(f"--serial={game['id']}")
        if enhance_dir:
            enhance_dir.mkdir(parents=True, exist_ok=True)
            cmd.append(f"--enhance-dir={enhance_dir}")

    print(
        f"ARMSX_BOOT begin id={game['id']} frames={frames} enhance={'on' if enhance else 'off'}",
        flush=True,
    )
    result = subprocess.run(cmd, cwd=ROOT, check=False, capture_output=True, text=True)
    # Emulator chatter goes to stdout mixed with JSON; keep stderr for real errors.
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        print(
            f"ARMSX_BOOT failed id={game['id']} exit={result.returncode}",
            file=sys.stderr,
            flush=True,
        )
        return 1

    events = parse_json_events(result.stdout)
    start = next((ev for ev in events if ev.get("event") == "start"), {})
    done = next((ev for ev in events if ev.get("event") == "done"), {})
    serial = start.get("serial") or done.get("serial") or ""
    print(
        f"ARMSX_BOOT serial={serial or 'none'} enhance={start.get('enhance')} "
        f"applied={done.get('enhance_applied')} assets={done.get('assets')}",
        flush=True,
    )
    hashes = {
        (ev["frame"], ev["w"], ev["h"]): ev["hash"]
        for ev in events
        if ev.get("event") in ("hash", "capture") and "hash" in ev
    }

    failed = 0
    for exp in game["expect"]:
        key = (exp["frame"], exp["w"], exp["h"])
        got = hashes.get(key)
        if got is None:
            print(
                f"ARMSX_BOOT failed id={game['id']} frame={exp['frame']} "
                f"reason=missing-hash expected={exp['hash']}",
                file=sys.stderr,
                flush=True,
            )
            failed += 1
            continue
        if expect_diff:
            if got == exp["hash"]:
                print(
                    f"ARMSX_BOOT failed id={game['id']} frame={exp['frame']} "
                    f"reason=enhance-matched-golden hash={got}",
                    file=sys.stderr,
                    flush=True,
                )
                failed += 1
                continue
            print(
                f"ARMSX_BOOT passed id={game['id']} frame={exp['frame']} "
                f"{exp['w']}x{exp['h']} hash={got} differ=golden",
                flush=True,
            )
            continue
        if got != exp["hash"]:
            print(
                f"ARMSX_BOOT failed id={game['id']} frame={exp['frame']} "
                f"{exp['w']}x{exp['h']} got={got} expected={exp['hash']}",
                file=sys.stderr,
                flush=True,
            )
            failed += 1
            continue
        print(
            f"ARMSX_BOOT passed id={game['id']} frame={exp['frame']} "
            f"{exp['w']}x{exp['h']} hash={got}",
            flush=True,
        )

    return 1 if failed else 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Boot local discs and check golden hashes.")
    parser.add_argument("--game", action="append", dest="games", help="serial or name substring")
    parser.add_argument("--bios", help="override BIOS path")
    parser.add_argument("--capture-dir", help="optional PNG dump directory")
    parser.add_argument("--enhance", action="store_true", help="enable presentation-layer enhancement")
    parser.add_argument("--enhance-dir", help="cache root for enhancement assets")
    parser.add_argument(
        "--expect-diff",
        action="store_true",
        help="require title hashes to differ from the unenhanced goldens",
    )
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()

    spec = load_goldens()
    if args.list:
        for g in spec["games"]:
            print(f"{g['id']:16}  {g['name']}  cue={g['cue']}")
        return 0

    if not QA_BIN.is_file():
        print("ARMSX_BOOT failed reason=no-qa-binary (run: make qa)", file=sys.stderr)
        return 1

    bios = find_bios(spec, args.bios)
    if bios is None:
        print("ARMSX_BOOT skip reason=no-bios (place SCPH-1001 at bios/PSX - SCPH1001.BIN)")
        return 0

    wanted = spec["games"]
    if args.games:
        keys = [k.lower() for k in args.games]
        wanted = [
            g
            for g in spec["games"]
            if any(k in g["id"].lower() or k in g["name"].lower() for k in keys)
        ]
        if not wanted:
            print(f"ARMSX_BOOT failed reason=unknown-game {args.games}", file=sys.stderr)
            return 2

    capture = Path(args.capture_dir) if args.capture_dir else None
    enhance_dir = Path(args.enhance_dir) if args.enhance_dir else None
    ran = 0
    failed = 0
    for game in wanted:
        if not (ROOT / game["cue"]).is_file():
            print(f"ARMSX_BOOT skip id={game['id']} reason=no-cue")
            continue
        ran += 1
        failed += run_game(
            bios,
            game,
            capture,
            enhance=args.enhance,
            enhance_dir=enhance_dir,
            expect_diff=args.expect_diff,
        )

    if ran == 0:
        print("ARMSX_BOOT skip reason=no-discs (extract BIN/CUE under roms/)")
        return 0
    if failed:
        print(f"ARMSX_BOOT failures={failed} ran={ran}")
        return 1
    print(f"ARMSX_BOOT all present games passed ran={ran}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
