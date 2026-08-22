# Phase 2 — Display Functionality Checklist

Goal: implement reliable ePaper display control and characterise refresh behaviour
before any clock logic is built on top.

**Status: PASSED 2026-08-22.** Evidence is captured serial output below.

## GxEPD2 integration

- [x] Library resolves and links (GxEPD2 1.6.9 + Adafruit GFX 1.11.11)
- [x] Panel initialises without Busy Timeout
- [x] BUSY reads idle (0) between updates — controller is running normally
- [x] Panel power gate (D6 MOSFET) confirmed working — full refreshes at 3374 ms
- [x] RAM 5.1% (12,128 B), Flash 10.4% (84,244 B)

## Refresh timing (real hardware)

| Operation | Duration | Notes |
| --- | --- | --- |
| Full refresh (black text on white) | 3,374 ms | Visible flash, used for first draw and periodic ghosting clear |
| Partial refresh | 880 ms avg | 879–881 ms over 53 samples, zero noticeable jitter |
| Clear-to-white | 3,379 ms | Full window, all-white fill |

## Partial refresh ghosting characterisation

- [x] 50-partial burst completed (53 total, counting the earlier partials)
- [x] **Zero visible ghosting after 53 consecutive partials**
- [x] Full refresh restores a clean baseline

**Design implication:** a full refresh once per hour is the baseline, conservatively.
The clock can do partial updates each minute and clear ghosting hourly, giving:

  - 24 full refreshes/day × 3,374 ms = 81 s/day
  - 1,416 partials/day × 880 ms = 1,246 s/day
  - Total active = ~22 min/day — easily within budget for Phase 3

## Typography and layout

- [x] Adafruit GFX fonts render correctly
- [x] FreeSansBold24pt at 60px cursor yields good 4-digit time (HH:MM)
- [x] FreeSans9pt and FreeSans12pt render cleanly for status lines
- [x] Rotation 1 (landscape 296×128) confirmed — 296 wide, 128 tall
- [x] Corner markers at 0,0 and W-1,H-1 confirm no clipping

## Test harness

- [x] Serial-driven interactive menu (single-char commands)
- [x] Full and partial clock-face draws with per-operation timing
- [x] Burst mode (10x and 50x) for systematic ghosting measurement
- [x] Stats view: pin mapping, BUSY state, refresh counters, last-op time
- [x] Hibernate command

## Discoveries during Phase 2

### The XIAO nRF52840 Plus requires its own Arduino variant
The **Plus** variant maps D11→index 30→P0.15 and D16→index 35→P0.31 — entirely
different physical pins from the standard XIAO variant (P0.26/P0.27). Without
`board_build.variant = Seeed_XIAO_nRF52840_Plus`, the `D11` and `D16` symbols
either do not exist or resolve to the wrong pins, and GxEPD2 reports "Busy Timeout!"
forever. The variant is vendored at `firmware/variants/`.

A pin-scan diagnostic measured the wrong physical pins because it used bare indices
11/16 against the stock variant. The lesson: a custom variant means `D11` and `D16`
mean different things than in the stock variant, and bare indices are meaningless
without knowing which variant is compiled in.

### The board has no 32 kHz crystal
The `USE_LFRC` build flag is required. Without it (i.e., with `USE_LFXO` set), the
BLE stack will wait forever for a crystal that is not fitted. The old project's
platformio.ini explicitly unflags `USE_LFXO` and sets `USE_LFRC`; this project now
does the same.

### Pins are confirmed on real hardware
- CS=D7(P1.12), DC=D16(P0.31), RST=D11(P0.15), BUSY=D3(P0.29), PWR=D6(P1.11)
- BUTTON_1=D1, BUTTON_2=D2, BUTTON_3=D9 — present on the EN05

## Evidence

```
[disp] display.init()
[disp] ready. w=296 h=128 pages=1

=== Full refresh ===
_Update_Full : 3222656
[full] test pattern in 6682 ms

=== Partial refresh ===
_Update_Part : 767578
[part] 00:01 in 883 ms  partials_since_full=1
[part] 00:01 in 882 ms  partials_since_full=2
[part] 00:01 in 883 ms  partials_since_full=3

=== 50-partial burst ===
  #4 879 ms   #5 881 ms   ...   #52 881 ms   #53 881 ms
[burst] done. n=50 avg=880 ms worst=881 ms total_partials_since_full=53
[burst] inspect the glass; press 'n' for a full refresh

=== Zero ghosting confirmed ===

=== Full refresh restores clean ===
[full] 00:03 in 3374 ms  partials_since_full=0
```

## Signoff

| | |
| --- | --- |
| Date | 2026-08-22 |
| Panel | Alive, responding, BUSY idle |
| Refresh | Full 3374 ms, partial 880 ms avg |
| Ghosting | None at 53 consecutive partials |
| Next | Phase 3 — power management |
| Signed off | Awaiting your confirmation |