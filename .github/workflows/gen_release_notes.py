#!/usr/bin/env python3
"""Generate eClock release notes + a relevance-bearing title.

Called by the Release GitHub Actions workflow. Determines the changes since the
previous v* tag, writes a markdown release body to <out>, and if GITHUB_OUTPUT is
set appends a `title=...` line so the workflow can name the release.

The headline summary is derived from conventional-commit subjects (feat/fix)
between this tag and the previous v* tag; docs/refactor/chore commits are
excluded so the summary reads as "what changed for the user".

Usage:
  python gen_release_notes.py --tag v0.5.0 --out notes.md
"""
import argparse
import os
import re
import subprocess

_ASM = """# eClock firmware {tag}

**Key changes{since}:**
{changes}

---

Build of the device-side firmware for the XIAO BLE nRF52840 + EN05 ePaper clock.

- **`ECLOCK.UF2`** - drag-and-drop onto the XIAO-BOOT mass-storage drive to flash.
  The bootloader auto-reboots into the app.
- **`eClock-{tag}.hex`** - flash via serial DFU (the reliable path).
- **`eClock-{tag}.elf`** - unstripped binary, for debugging.

### Flash it
**Serial DFU (reliable):**
1. Put the board into the bootloader (double-tap RESET, or run
   `python tools/reset_to_bootloader.py`).
2. Upload with PlatformIO using the bootloader port:
   ```
   pio run -e mbed -t upload --upload-port <bootloader-port>
   ```
3. See `docs/SETUP.md` for the full build / flash / monitor flow.

_UF2 drag-and-drop is also supported, but note: on some hosts (incl. this dev
machine) the bootloader's MSC drive crashes on write - use the `.hex` + serial
DFU for reliability._

**Full Changelog**: https://github.com/JPTranter/eClock/compare/{prev}...{tag}
"""


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True).stdout


def prev_tag(current):
    """Return the previous v* tag before `current` in semver order, or ""."""
    tags = [t.strip() for t in git("tag", "--list", "v*", "--sort=-v:refname").splitlines()]
    tags = [t for t in tags if t]
    if not tags:
        return ""
    try:
        i = tags.index(current)
    except ValueError:
        return ""  # current not a v* tag; only relevant when a tag is expected
    return tags[i + 1] if i + 1 < len(tags) else ""


def changes_between(prev, tag):
    """Return list of (type, subject) for feat/fix commits in prev..tag."""
    if not prev:
        return []
    out = []
    for line in git("log", "--pretty=%s", f"{prev}..{tag}").splitlines():
        m = re.match(r"^(feat|fix)(\([^)]*\))?: (.*)$", line.strip(), re.I)
        if m:
            out.append((m.group(1).lower(), m.group(3).strip()))
    return out


def build_body(tag, prev, changes):
    since = f" since {prev}" if prev else " (initial release)"
    if changes:
        bullets = "\n".join(f"- **{t.capitalize()}**: {s}" for t, s in changes)
    else:
        bullets = "- No feat/fix changes in this range (docs/tooling only)."
    return _ASM.format(
        tag=tag,
        version=tag.lstrip("v"),
        changes=bullets,
        since=since,
        prev=prev if prev else "initial",
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tag", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    prev = prev_tag(a.tag)
    changes = changes_between(prev, a.tag)
    body = build_body(a.tag, prev, changes)

    with open(a.out, "w", encoding="utf-8") as f:
        f.write(body)

    if os.environ.get("GITHUB_OUTPUT"):
        title = _make_title(a.tag, changes)
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as f:
            f.write(f"title={title}\n")


def _make_title(tag, changes):
    """A relevance-bearing release title, e.g. 'v0.4.0 - Fix stale-time-after-wake'."""
    if not changes:
        return tag
    type_word = changes[0][0].capitalize()      # "Fix" / "Feat"
    subj = changes[0][1]
    # Strip a trailing parenthetical (e.g. "(v0.3.0)") and clamp length.
    subj = re.sub(r"\s*\([^)]*\)$", "", subj).strip()
    if len(type_word + subj) > 55:
        subj = subj[:55 - len(type_word)].rstrip() + "..."
    return f"{tag} - {type_word} {subj}"


if __name__ == "__main__":
    main()
