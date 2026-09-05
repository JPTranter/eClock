# eClock Power Budget Analysis

> Date: 2026-08-25
>
> Answers the open questions in `RESEARCH_TRACKING.md` ("Power management /
> deep sleep — highest project risk") and `STATUS.md` ("does a minute-resolution
> clock fit inside a 500 mAh budget for three months?").
>
> **Status: MODEL ONLY.** Every timing value below is measured on real hardware;
> every *current* value is an assumption and is flagged as such. Nothing here
> has been confirmed with a multimeter or PPK2 yet.

## Headline

At the current 1-minute refresh, battery life on a 500 mAh cell is **bounded
between roughly 1 and 6 months**, and the entire spread is driven by two
numbers that have never been measured:

1. the current the e-paper panel draws during its 880 ms partial refresh
   (assumed 4–40 mA), and
2. the full-board idle current in System-ON WFE sleep (assumed 3–50 µA).

The 3-month goal (`PROJECT_PLAN.md`) is **not guaranteed at 1-minute refresh**.
It becomes robust across the whole credible range by slowing the refresh to
5–10 minutes, which is a one-line change (`TICK_INTERVAL`).

## What the firmware actually does

Traced from `firmware/src/main.cpp`:

| Window | State | Power-relevant behaviour |
| --- | --- | --- |
| boot | `SYNCING` | full draw + BLE advertising (~12 mA) until HA writes the time (1–3 s) |
| 05:00–23:00 | `RUNNING` | partial refresh (880 ms) every 60 s; battery ADC each tick; BLE off except an hourly ~2 s re-sync; WFE idle between ticks; **panel rail (D6) powered the whole 18 h** |
| 23:00–05:00 | `SLEEPING` | one full "Zzz" draw (3.4 s); BLE off; panel rail cut (D6 LOW); WFE sleep up to 6 h; button wake keeps the clock on until the next 11pm |
| 05:00 | `RESYNCING` | panel re-powered, full init (3.4 s clear), BLE re-sync |

Measured timing (from `docs/lessons`, Phases 2–3):

| Operation | Duration | Evidence |
| --- | --- | --- |
| Partial refresh | 880 ms | 53 samples, 879–881 ms, zero jitter |
| Full refresh | 3,374 ms | hardware clear + draw |

Consequences baked into the model:

- **1080** partial refreshes/day (18 h × 60)
- **2** full refreshes/day (the 23:00 "Zzz" draw + the 05:00 re-init clear)
- **18** BLE syncs/day, ~2 s each (HA answers in 1–3 s; the 30 s
  `SYNC_TIMEOUT` is only exercised when HA is down)

## The model

Daily energy is the sum of six terms. The timing is measured; the current in
each term is assumed.

| Quantity | Value | Class | Confidence |
| --- | --- | --- | --- |
| Partial refresh duration | 880 ms | measured | high |
| Full refresh duration | 3,374 ms | measured | high |
| Partials/day | 1080 | derived | high |
| BLE advertising current | 12 mA | assumed | medium (docs figure, plausible) |
| **Partial-refresh current** | 4–40 mA (docs assume ~8) | **assumed** | **low — the main risk** |
| **System-ON WFE idle current** | 3 µA chip-only, 10–60 µA full board | **assumed** | **low** |
| Panel hibernate current | 3 µA | assumed | low |
| CPU active (ADC + framebuffer) | 4 mA for ~30 ms/refresh | assumed | low |

### Two scenarios (endpoints of the credible range)

| Term | Optimistic<br>(4 mA / 3 µA) | Realistic<br>(40 mA / 50 µA) |
| --- | ---: | ---: |
| Partial refresh (1080/day) | 1.06 mAh/day | 10.6 mAh/day |
| Full refresh (2/day) | 0.01 mAh/day | 0.08 mAh/day |
| CPU + ADC (30 ms × 1080) | 0.04 mAh/day | 0.04 mAh/day |
| BLE re-sync (18 × 2 s) | 0.12 mAh/day | 0.12 mAh/day |
| Panel hibernate (18 h × 3 µA) | 0.05 mAh/day | 0.05 mAh/day |
| WFE idle (~23.7 h) | 0.07 mAh/day | 1.19 mAh/day |
| **TOTAL** | **1.34 mAh/day** | **12.0 mAh/day** |
| Average current | 56 µA | 501 µA |
| **Battery life (450 mAh usable)** | **~335 days** | **~37 days** |

*"450 mAh usable" = 500 mAh cell × 90%, allowing for regulator cutoff and the
fact that a LiPo cannot be drained to 0 V.*

## Sensitivity

Battery life in **days**, full matrix over the two unmeasured constants:

| refresh mA \ idle µA | 3 µA | 50 µA | 150 µA | 500 µA |
| --- | ---: | ---: | ---: | ---: |
| 4 mA | 335 | 183 | 93 | 34 |
| 8 mA | 187 | 128 | 76 | 32 |
| 15 mA | 105 | 84 | 58 | 28 |
| 40 mA | 41 | 37 | 31 | 20 |

Refresh frequency vs battery life:

| Resolution | Optimistic<br>(4 mA / 3 µA) | Realistic<br>(40 mA / 50 µA) |
| --- | ---: | ---: |
| 1 min (current) | ~335 days | ~37 days |
| 5 min | ~954 days | ~126 days |
| 7.5 min | ~1128 days | ~157 days |
| 10 min | ~1241 days | ~179 days |

## Reconciliation with the docs' earlier estimates

The repo already contains two estimates: §9 of `LESSONS_LEARNT.md` (~28 days)
and §14 (~37 days). This model **reproduces** them rather than contradicting
them. §14's "display refresh 625 µA day average" line back-computes to ~56 mA
averaged over the 880 ms refresh window — i.e. the earlier figures assumed the
panel draws tens of milliamps during refresh, the conservative end of the range
above.

So the honest reading is: the docs' own ~28–37 day figures and this model's
"realistic" scenario (~37 days) are the same number reached two different ways.
The optimistic ~6-month figure only materialises if the panel really averages
~4–8 mA *and* the board really idles at ~3 µA — neither of which is proven.

## Key findings

1. **Refresh current dominates.** 1080 refreshes/day = ~950 s/day of
   panel-active time; every 1 mA of refresh current costs ~0.26 mAh/day.

2. **The "3 µA" idle figure is chip-only.** It excludes the XIAO's onboard
   regulator, whose quiescent current is more likely 10–60 µA — which alone is
   0.2–1.2 mAh/day, comparable to or larger than the BLE term. This is why the
   earlier "3 µA" assumption was flattering.

3. **3 months is out of reach at 1-minute refresh.** The budget is ≤5.0 mAh/day
   (450 mAh / 90 days). Fixed non-refresh costs (idle + BLE + full refreshes +
   panel) consume 0.3–1.5 mAh/day, leaving the refresh budget ~3.5–4.7 mAh/day.
   At 1080 refreshes/day that demands the panel average **≤ ~13–18 mA** during
   refresh; real 2.9" e-paper draws 20–40 mA with boost-converter peaks.

4. **Slowing the refresh is the highest-leverage change, and it is one
   constant.** At 5-minute refresh the 3-month target is cleared even at the
   pessimistic end of the current range; at 10 minutes it is bulletproof.

5. **(Side observation) The firmware never does a daytime full refresh.**
   Between 05:00 and 23:00 the clock uses partial refresh exclusively. The
   docs validated "zero ghosting" only out to 53 consecutive partials; 1080/day
   with no full refresh is unvalidated. Adding one full refresh/hour would cost
   ~0.01–0.08 mAh/day (negligible) and buys ghosting headroom — worth deciding
   deliberately rather than inheriting by omission.

## What to measure (this closes the model)

Three measurements turn the 1–6 month range into a number:

1. **Full-board idle current** (panel powered, WFE sleep). Series multimeter or
   a Nordic PPK2 at the battery terminals. Expect 10–60 µA, not 3.
2. **Average current during one partial refresh** (the 880 ms window). Confirms
   whether it is ~8 mA or ~40 mA.
3. **SSD1680 idle/standby current with the rail on.** Does GxEPD2 put the
   controller into deep sleep between refreshes, or is it drawing standby all
   day?

Method note: measure in series with the *battery*, not the USB supply. The
panel and its booster sit downstream of the regulator, so a battery-terminal
meter captures the whole board; a USB meter does not see the battery's own
self-discharge or the charging path.

## Recommended changes

1. **Implemented (v0.5.0):**
   - Explicit `display.powerOff()` after each partial draw to disable SSD1680 high-voltage booster/charge-pump between ticks (Lesson §44).
   - Enabled nRF52 internal DC-DC converter (`NRF_POWER->DCDCEN = 1`), reducing active dynamic power during CPU rendering and BLE by ~30–45% (Lesson §45).
   - Reduced battery ADC resistor divider delay from 10 ms to 50 µs (`delayMicroseconds(50)`), cutting resistive drain on-time by 200× (Lesson §47).
2. Set `TICK_INTERVAL` to `300000` (5 min) — comfortable 3-month margin at any
   credible current. Product trade-off: the minute digit only changes every 5
   minutes.
3. Decide on a periodic daytime full refresh (e.g. hourly) for ghosting
   headroom, at negligible power cost.
4. If measurement (1) comes back high (>20 µA), revisit cutting the panel rail
   between refreshes — but note the 3.4 s full re-init cost each wake
   (Phase 3 log, "partial refresh only works within a single power-on session").

## Appendix: reproducible model

```python
# Power budget model — timing measured, currents parameterised.
PARTIAL_S   = 0.880          # measured
FULL_S      = 3.374          # measured
PARTIALS    = 1080           # 18h * 60
FULLS       = 2              # night icon + morning re-init
BLE_MA      = 12.0           # assumed
BLE_N       = 18             # hourly while running
BLE_S       = 2.0            # HA answers in 1-3s
PANEL_UA    = 3.0            # assumed hibernate
CPU_MA      = 4.0            # assumed
CPU_S       = 0.03           # ADC (10ms) + framebuffer per refresh
BATTERY     = 500.0
USABLE      = 0.9

def daily(refresh_ma, idle_ua, partials=PARTIALS):
    e_partial = partials * PARTIAL_S * refresh_ma / 3600.0
    e_full    = FULLS * FULL_S * refresh_ma / 3600.0
    e_cpu     = partials * CPU_S * CPU_MA / 3600.0
    e_ble     = BLE_N * BLE_S * BLE_MA / 3600.0
    e_panel   = 18.0 * PANEL_UA / 1000.0
    active_s  = partials * (PARTIAL_S + CPU_S) + FULLS * FULL_S
    idle_s    = 86400.0 - active_s
    e_idle    = idle_s * idle_ua / 3.6e6
    return e_partial + e_full + e_cpu + e_ble + e_panel + e_idle

def life(refresh_ma, idle_ua, partials=PARTIALS):
    return USABLE * BATTERY / daily(refresh_ma, idle_ua, partials)

if __name__ == "__main__":
    print(f"optimistic 4mA/3uA : {life(4, 3):.0f} days")
    print(f"realistic 40mA/50uA: {life(40, 50):.0f} days")
    for per_day, label in [(1080, "1 min"), (216, "5 min"),
                           (144, "7.5 min"), (108, "10 min")]:
        print(f"{label:>6s}: opt {life(4, 3, per_day):.0f}d  "
              f"real {life(40, 50, per_day):.0f}d")
```
