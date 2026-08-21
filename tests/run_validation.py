#!/usr/bin/env python3
"""Default automated validation for ARMSX1 / the Super Enhancement Engine.

Cases that need cmake or SDL2 are skipped (not failed) when those tools are
missing on the host. SEE unit tests always run. Real-title boot runs when
bios/ and roms/ are present and skips cleanly when they are not.
"""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
CASES = {
    "source": ([sys.executable, str(ROOT / "tests" / "validate_sources.py")], 30.0),
    "cpu": (["make", "test-cpu"], 180.0),
    "audio": (["make", "test-audio"], 30.0),
    "qa": (["make", "test-qa"], 180.0),
    "see": (["make", "test-see"], 60.0),
    "vk": (["make", "test-vk"], 60.0),
    "boot": (["make", "test-boot-see"], 360.0),
    "gpu": (["make", "test-gpu"], 120.0),
    "sdl-audio": (["make", "test-sdl-audio"], 30.0),
    "chd": (["make", "test-chd"], 180.0),
    "zip": (["make", "test-zip"], 180.0),
}

SDL_CASES = {"gpu", "sdl-audio"}
CMAKE_CASES = {"chd", "zip"}
VK_HEADER = ROOT / "third_party" / "vulkan-headers" / "include" / "vulkan" / "vulkan.h"


def host_skip_reason(name: str) -> str | None:
    if name in SDL_CASES and not shutil.which("sdl2-config"):
        return "host-missing-sdl2"
    if name in CMAKE_CASES and not shutil.which("cmake") and not (
        ROOT / "third_party" / "cmake" / "bin" / "cmake"
    ).is_file():
        return "host-missing-cmake"
    if name == "vk" and not VK_HEADER.is_file():
        return "host-missing-vulkan-headers"
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description="Run deterministic ARMSX validation cases.")
    parser.add_argument("--case", action="append", choices=tuple(CASES), dest="selected")
    parser.add_argument("--timeout-scale", type=float, default=1.0)
    args = parser.parse_args()
    if args.timeout_scale <= 0:
        parser.error("--timeout-scale must be greater than zero")

    selected = args.selected or list(CASES)
    failures: list[str] = []
    started = time.monotonic()

    for name in selected:
        skip = host_skip_reason(name)
        if skip:
            print(f"ARMSX_VALIDATION skip case={name} reason={skip}", flush=True)
            continue

        command, timeout = CASES[name]
        print(f"ARMSX_VALIDATION begin case={name}", flush=True)
        case_started = time.monotonic()
        try:
            result = subprocess.run(
                command,
                cwd=ROOT,
                check=False,
                timeout=timeout * args.timeout_scale,
            )
        except subprocess.TimeoutExpired:
            failures.append(f"{name}:timeout")
            print(f"ARMSX_VALIDATION failed case={name} reason=timeout", file=sys.stderr, flush=True)
            continue

        elapsed = time.monotonic() - case_started
        if result.returncode:
            failures.append(f"{name}:exit-{result.returncode}")
            print(
                f"ARMSX_VALIDATION failed case={name} exit={result.returncode} elapsed={elapsed:.3f}s",
                file=sys.stderr,
                flush=True,
            )
        else:
            print(f"ARMSX_VALIDATION passed case={name} elapsed={elapsed:.3f}s", flush=True)

    elapsed = time.monotonic() - started
    if failures:
        print(f"ARMSX_VALIDATION failures={','.join(failures)} elapsed={elapsed:.3f}s", file=sys.stderr)
        return 1
    print(f"ARMSX_VALIDATION all selected cases passed elapsed={elapsed:.3f}s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
