# Bottom-Message Feature — Design Record & Implementation Reference

> Status: **design settled (2026-08-29), implementation pending.** This is the
> reference for building the feature. The reference screenshot is
> `docs/screenshots/target_design_message_layout.png`.

## Goal
Allow Home Assistant to push a small text message (e.g. "Merry Christmas",
"Happy Birthday {name}") to the eClock, displayed on the clock face.

## Scope decisions (from the discussion)
- **Source:** Home Assistant provides the message (global — same for all clocks).
- **Config in HA:** a list of rules, each EITHER date-based or weekday-based:
  - Date-based: `{ message, month, day, year? }`. No `year` → fires every year
    (annual, e.g. Christmas); with `year` → fires that one date only (one-off).
  - Weekday-based: `{ message, weekday }` (weekday 0=Monday .. 6=Sunday, matching
    `datetime.weekday()`). Fires every week on that day — a **fallback**.
- **Message length cap:** **30 characters.** (Fits the whitespace before AM/PM with
  a 2-char margin; see below.)
- **Selection precedence:** a date-based message matching today WINS over a weekday
  message; if no date rule matches, the weekday message for today is the fallback.
  Within each group, the LAST matching entry in the list wins (a one-off can override
  an annual; a later weekday entry overrides an earlier one).
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

## Reference screenshot
`docs/screenshots/target_design_message_layout.png` shows the three target states
(no message, 31-char which reveals the overflow risk, sync-failed + message).
Keep it until implementation is complete, as the design history.

## Backend / protocol (for implementation)
- Add a new writable BLE characteristic for the message (separate UUID), on top of the
  existing Current Time characteristic.
- HA writes time (0x2A2B, 8 bytes) first, then the message to the new characteristic
  best-effort.
- Firmware caches the current day's message; if `year` present and today is that date,
  render; else blank (normal clock face).

## Open items before implementation
- Exact new characteristic UUID + value format (fixed 30-char? null-padded? length-prefixed?).
- HA config schema (list of `{message, month, day, year?}`) and where it's declared.
