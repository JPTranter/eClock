# Bottom-Message Feature — Design Record & Implementation Reference

> Status: **implemented (2026-08-29).** This records the design and the settled
> wire contract. The final approved layouts are listed under "Reference screenshots".

## Goal
Allow Home Assistant to push a small text message (e.g. "Merry Christmas",
"Happy Birthday {name}") to the eClock, displayed on the clock face.

## Scope decisions (from the discussion)
- **Source:** Home Assistant provides the message (global — same for all clocks).
- **Config in HA:** a list of entries, each `{ message }` plus an optional natural
  `day` rule:
  - **Default:** `{ message }` only — lowest-precedence fallback, shown whenever
    nothing more specific matches.
  - **Weekly:** `{ message, day: FRIDAY_NAME }` (e.g. `Sun`, `Fri`, or full name) —
    fires every week on that weekday (fallback below date rules).
  - **Annual:** `{ message, day: "25 Dec" }` — fires every year on that date.
  - **One-off:** `{ message, day: "14 Mar 2026" }` — fires only on that date (highest
    precedence among date rules).
  - `day` is case-insensitive. Only `message` and `day` keys are supported. Invalid
    entries (missing message, unrecognised key, unparseable `day`) are reported as
    errors in the Home Assistant log and skipped — never silently ignored.
- **Message length cap:** **30 characters.** (Fits the whitespace before AM/PM with
  a 2-char margin; see below.)
- **Selection precedence** (highest → lowest):
  1. One-off / annual date rules matching today; among date entries, the LAST in the
     list wins.
  2. A weekday rule matching today's weekday (fallback); last in list wins.
  3. A default message (no rule) — shown when nothing above matches.
- **Where it displays:** the date/battery/sync stay on a single top row; the message
  occupies the freed bottom row, **left-aligned at x=2**, with AM/PM at the right.
- **Backward compatibility:** the message must be an **additive** BLE characteristic
  (never change the existing 8-byte `0x2A2B` time sync). Old clocks ignore it; new HA
  must write time first then message best-effort.

## Settled visual layout (top row — single right-aligned group)
The sync status and power display are rendered as **ONE right-aligned group**:
```
[optional sync time] [sync icon] <-2px-> [optional battery %] [battery icon]
```
- Right-anchored to the screen edge (x=292); the block flows left with uniform gaps
  between whatever components are present; always hugs the edge.
- **Sync icon top edge flush** at screen top (y=0).
- Text **baseline = 14** (so text vertically centres on the flush-top icon).
- **Sync icon → % = 2px ink gap** (measured, not box-math — the % glyph has ~4px
  left sidebearing, so use ink placement).
- **Battery icon** centred on the sync icon's visible ink centre (y≈8); the battery
  bitmap has blank rows at its bottom, so align by *visible* cell ink, not nominal
  centre.
- Battery icon **raised/positioned** so its visible centre matches the sync icon
  (within ~0.5px rounding).
- % ↔ battery icon gap: 3px.

### The 4 component states
| State | Components shown |
|---|---|
| Sync OK | `[icon] [100%] [battery]` |
| Sync FAILED | `[23:59] [icon] [100%] [battery]` (last-sync time appears) |
| USB power | `[icon] [battery]` (no %, bolt implied) — no % |
| No time / low battery | (existing screens unchanged) |

## Message row
- **Left-aligned** at x=2.
- **Baseline** y=121 (9pt FreeSans).
- **AM/PM** at bottom-right (unchanged).

## CRITICAL: overflow protection (the 30-char limit)
The message must **never collide with AM/PM**. From measurement:
- AM/PM left ink edge ≈ x=250.
- A 31-char message ("Happy Birthday Alexandra Maxime") right edge hit x=247 → only
  a **2px ink gap**. Too tight.
- Whitespace before AM/PM ≈ x=2..249 (248px). At ~7.3px/char, that's ~34 chars
  theoretical, but we want margin.

**Protection rule (to implement):**
- Enforce a **minimum 2 whitespace-character gap** between the message ink right edge
  and the AM/PM ink left edge (i.e. message right edge must be ≤ AM/PM_left − 2px).
- If a message would exceed that, **truncate it** (preferably on the **HA side** so the
  clock never receives an over-long message; the firmware also clamps defensively).
- Practical safe cap: **30 characters** (leaves ~2 char / ~15px margin to AM/PM).

## Reference screenshots
Implementation is complete. The final approved layouts are:
- `docs/screenshots/running.png` — normal clock face (no message).
- `docs/screenshots/message.png` — clock face with a bottom message.
- `docs/screenshots/running_usb.png` — USB/charging (bolt icon, no %).
- `docs/screenshots/running_low_battery.png` — low battery (empty icon).
- `docs/screenshots/state_matrix_approval.png` — every sync × power top-right combo.

## Backend / protocol (for implementation)
- New writable BLE characteristic for the message (separate UUID), on top of the
  existing Current Time characteristic.
- HA writes time (0x2A2B, 8 bytes) first, then the message to the new characteristic
  best-effort.
- Firmware caches the current message string and renders it on the bottom row.

## Implemented contract (resolved open items)
- **Characteristic UUID:** `c0dec10c-2a2c-4a20-8c10-000000000000` (custom 128-bit,
  additive; never touches the 8-byte `0x2A2B` time sync).
- **Value format:** fixed-width **30-byte NUL-padded ASCII** payload. HA truncates to
  30 chars (server-side); the firmware reads up to 30 bytes, stops at the first NUL,
  and NUL-terminates. An all-NUL payload clears the line.
- **HA config:** `epaper_clock:` with a `messages:` list — natural `day:` rules
  (default / weekly / `day: "25 Dec"` annual / `day: "14 Mar 2026"` one-off) as
  documented above. Invalid entries are logged as errors in HA.
- **Firmware render:** message rendered **left-aligned at x=2** on the freed bottom
  row, baseline y=121, clamped so it never collides with AM/PM. `drawRunningFace`
  was reworked to the settled layout: single right-aligned top group
  `[date] [sync time?][sync icon]<2px>[%][battery icon]` with the sync icon top edge
  flush at y=0 and text baseline 14, and the previous bottom-left sync status removed
  (it moved up into the group).
