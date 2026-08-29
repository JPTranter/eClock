#!/usr/bin/env python3
"""Regenerate every image used in the documentation, in one shot.

The docs screenshots in ``docs/screenshots/`` are real renders produced by the HOST
test harness (``firmware/test/``). They drift out of date whenever a screen renderer,
font, icon, or layout changes — and, as happened once, when *some* test binaries are
rebuilt but not others. This script rebuilds and re-runs the relevant suites, copies
the fresh renders to ``docs/screenshots/`` (applying the correct mapping), assembles
the composite state matrix, and verifies every image referenced by the docs exists and
is a valid display-size image.

Usage (from the repo root):
    python firmware/tools/regenerate_docs_screenshots.py [--skip-assembly] [--verbose]

Requirements:
    * The host test fixture must be configured and built once
      (cmake -S firmware/test -B firmware/test/build -G Ninja && cmake --build firmware/test/build).
    * PIL/Pillow is only needed for the composite state-matrix assembly; if it is not
      installed the script regenerates the single-screen images and skips the matrix
      with a warning (pass --skip-assembly to suppress the matrix entirely).

Mapping from harness output to docs/screenshots:
    output/running.png            -> running.png
    output/syncing.png            -> syncing.png
    output/no_time.png            -> no_time.png
    output/running_usb.png        -> running_usb.png
    output/running_low_battery.png-> running_low_battery.png
    output/low_battery.png        -> low_battery.png
    output/sleep_icon.png         -> sleeping.png        (the OVERNIGHT screen, not
                                                          output/sleeping.png which is
                                                          the defensive Zzz-only one)
    output/cd_running_message.png -> message.png         (the pure-module message render)
    output/cd_state_{ok,syncing,failed,low,usb,fail_low}.png -> state_matrix_approval.png
                                                          (assembled composite)
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
FW_TEST = REPO_ROOT / "firmware" / "test"
BUILD = FW_TEST / "build"
OUT = FW_TEST / "output"
SCREENSHOTS = REPO_ROOT / "docs" / "screenshots"

# The host test executables that dump the PNGs used by the docs.
#   test_display       -> the main.cpp harness screens (running/syncing/no_time/...)
#   test_clock_display -> the pure-module screens (cd_running_message) + cd_state_*
TEST_BINARIES = ["test_display", "test_clock_display"]

# Render source -> docs destination (applied AFTER the suites run).
SCREEN_MAP = [
    ("running.png", "running.png"),
    ("syncing.png", "syncing.png"),
    ("no_time.png", "no_time.png"),
    ("running_usb.png", "running_usb.png"),
    ("running_low_battery.png", "running_low_battery.png"),
    ("low_battery.png", "low_battery.png"),
    # Overnight screen is sleep_icon.png (Zzz + "Sleeping"), NOT sleeping.png (Zzz only).
    ("sleep_icon.png", "sleeping.png"),
    # The message render is a pure-module render (cd_ prefix).
    ("cd_running_message.png", "message.png"),
]

# The six state-matrix sources, in the order they appear top-to-bottom in the composite.
MATRIX_SOURCES = [
    ("cd_state_ok.png", "Sync OK + 100%"),
    ("cd_state_syncing.png", "Syncing + 100%"),
    ("cd_state_failed.png", "Sync FAILED + time + 100%"),
    ("cd_state_low.png", "Sync OK + LOW 15% (empty icon)"),
    ("cd_state_usb.png", "USB (bolt, no %)"),
    ("cd_state_fail_low.png", "FAILED + LOW"),
]
MATRIX_OUT = "state_matrix_approval.png"


def log(*args) -> None:
    print("[docs-screenshots]", *args)


def run_harness_suite() -> None:
    """Build the two PNG-producing executables and run them to dump fresh renders."""
    import platform

    env = os.environ.copy()
    # Note: on Windows the MinGW runtime DLLs are bundled next to each test executable
    # by CMake (see firmware/test/CMakeLists.txt), so no PATH adjustment is needed here.

    # Build.
    log("building host test fixture...")
    subprocess.run(["cmake", "--build", str(BUILD)], check=True, env=env)

    # Run each PNG-producing executable so it dumps its renders to output/.
    for name in TEST_BINARIES:
        exe = BUILD / f"{name}.exe" if platform.system() == "Windows" else BUILD / name
        if not exe.exists():
            log(f"WARNING: {exe} not found; skipping {name}")
            continue
        log(f"running {name}...")
        subprocess.run([str(exe)], check=True, env=env, cwd=str(FW_TEST))


def copy_screens() -> None:
    """Copy each fresh render from output/ into docs/screenshots/."""
    SCREENSHOTS.mkdir(parents=True, exist_ok=True)
    for src_name, dst_name in SCREEN_MAP:
        src = OUT / src_name
        if not src.exists():
            log(f"WARNING: {src} missing; skipping -> {dst_name}")
            continue
        shutil.copy2(src, SCREENSHOTS / dst_name)
        log(f"copied {src_name} -> docs/screenshots/{dst_name}")


def assemble_matrix() -> None:
    """Assemble the composite state matrix from the six cd_state_* renders."""
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        log("WARNING: Pillow not installed; skipping state-matrix assembly.")
        log("         Install it with: pip install pillow  (or pass --skip-assembly)")
        return

    imgs = []
    for src_name, label in MATRIX_SOURCES:
        src = OUT / src_name
        if not src.exists():
            log(f"WARNING: {src} missing; state matrix will be incomplete.")
            continue
        imgs.append((src, label))

    if not imgs:
        log("WARNING: no matrix source renders; skipping assembly.")
        return

    w, h = Image.open(imgs[0][0]).size
    lh, pad = 16, 6
    sheet = Image.new("RGB", (w, (h + lh + pad) * len(imgs) + pad), "white")
    d = ImageDraw.Draw(sheet)
    y = pad
    for src, label in imgs:
        sheet.paste(Image.open(src).convert("L").convert("RGB"), (0, y))
        y += h
        d.text((6, y + 2), label, fill=(0, 0, 0))
        y += lh + pad
    sheet.save(SCREENSHOTS / MATRIX_OUT)
    log(f"assembled state matrix -> docs/screenshots/{MATRIX_OUT}")


def verify() -> None:
    """Confirm every docs-referenced screenshot exists and is a valid panel-size image.

    The single-screen renders must be exactly 296x128 (the panel's resolution). The
    state matrix is a taller composite and is checked only for validity, not size.
    """
    try:
        from PIL import Image
    except ImportError:
        log("Pillow not installed; skipping image validation.")
        return

    # Images referenced in the docs.
    import re

    referenced = set()
    for md in REPO_ROOT.rglob("*.md"):
        text = md.read_text(encoding="utf-8", errors="replace")
        for m in re.finditer(r"(?:screenshots/|docs/screenshots/)([a-z_0-9]+\.png)", text):
            referenced.add(m.group(1))

    problems = []
    for name in sorted(referenced):
        path = SCREENSHOTS / name
        if not path.exists():
            problems.append(f"MISSING: {name}")
            continue
        try:
            im = Image.open(path)
            im.verify()
            if name == MATRIX_OUT:
                continue   # composite; any size is fine
            if im.size != (296, 128):
                problems.append(f"WRONG SIZE: {name} is {im.size}, expected 296x128")
        except Exception as exc:  # noqa: BLE001 - report any invalid image
            problems.append(f"INVALID: {name} ({exc})")

    if problems:
        log("VERIFICATION PROBLEMS:")
        for p in problems:
            log("  -", p)
        log("Check the render pipeline / mapping above.")
    else:
        log(f"OK: all {len(referenced)} referenced screenshots present and valid.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-assembly", action="store_true",
                        help="skip the state-matrix composite assembly")
    parser.add_argument("--verbose", action="store_true", help="print each step")
    args = parser.parse_args()

    if not (BUILD / ("test_display.exe" if os.name == "nt" else "test_display")).exists():
        log("FATAL: host test fixture not built. Configure/build it first:")
        log("  cmake -S firmware/test -B firmware/test/build -G Ninja")
        log("  cmake --build firmware/test/build")
        return 1

    run_harness_suite()
    copy_screens()
    if not args.skip_assembly:
        assemble_matrix()
    verify()
    log("done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
