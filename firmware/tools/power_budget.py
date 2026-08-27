#!/usr/bin/env python3
"""
eClock Power Budget Model

Reproducible daily energy model for the ePaper clock. Every timing value is
MEASURED from hardware (docs/lessons, Phases 2-3). Every current value is an
ASSUMPTION and is parameterised below; change them as bench measurements arrive.

Usage:
    python power_budget.py                 # typical scenario
    python power_budget.py --matrix        # full sensitivity matrix

See docs/research/power-budget-analysis.md for the full write-up.
"""

import sys

# ---- MEASURED timing (from docs/lessons) ----
PARTIAL_S   = 0.880        # partial refresh duration (53 samples, zero jitter)
FULL_S      = 3.374        # full refresh duration
DAYTIME_H   = 18.0         # awake 05:00 -> 23:00 (sleep window 23:00-05:00)
FULLS_DAY   = 2            # night "Zz" icon + morning re-init clear
BLE_SYNC_DAY = 18          # once/hour while running
BLE_SYNC_S  = 2.0          # HA answers 1-3s; 30s only if HA down
CPU_S       = 0.03         # ADC (10ms) + framebuffer draw per refresh

# ---- ASSUMED currents (UNMEASURED - the project's #1 risk) ----
REFRESH_MA  = 8.0          # panel current during a partial refresh
IDLE_UA     = 3.0          # full-board System-ON WFE idle (chip ~2uA + LDO)
BLE_MA      = 12.0         # BLE advertising current
CPU_MA      = 4.0          # CPU active (drawing framebuffer)
PANEL_UA    = 3.0          # SSD1680 hibernate current

BATTERY_MAH = 500.0
USABLE      = 0.9          # LiPo can't drain to 0V; regulator cutoff


def daily(refresh_ma=REFRESH_MA, idle_ua=IDLE_UA, partials=DAYTIME_H * 60):
    """Total mAh/day across all loads."""
    e_partial = partials * PARTIAL_S * refresh_ma / 3600.0
    e_full    = FULLS_DAY * FULL_S * refresh_ma / 3600.0
    e_cpu     = partials * CPU_S * CPU_MA / 3600.0
    e_ble     = BLE_SYNC_DAY * BLE_SYNC_S * BLE_MA / 3600.0
    e_panel   = DAYTIME_H * PANEL_UA / 1000.0
    active_s  = partials * (PARTIAL_S + CPU_S) + FULLS_DAY * FULL_S
    idle_s    = 86400.0 - active_s
    e_idle    = idle_s * idle_ua / 3.6e6
    return e_partial + e_full + e_cpu + e_ble + e_panel + e_idle


def life(refresh_ma=REFRESH_MA, idle_ua=IDLE_UA, partials=DAYTIME_H * 60):
    return USABLE * BATTERY_MAH / daily(refresh_ma, idle_ua, partials)


def main():
    if "--matrix" in sys.argv:
        print("Battery life in DAYS (450 mAh usable = 500 x 0.9)")
        print("rows = refresh current, cols = WFE idle current")
        hdr = "{:>14s}".format("refresh/idle")
        for u in [3, 50, 150, 500]:
            hdr += "{:>10d} uA".format(u)
        print(hdr)
        for r in [4, 8, 15, 40]:
            row = "{:>6d} mA     ".format(r)
            for i in [3, 50, 150, 500]:
                row += "{:>9.0f}d".format(life(r, i))
            print(row)
        print()
        print("Refresh-frequency table (typical 8mA / 3uA):")
        for per_day, label in [(1080, "1 min"), (216, "5 min"),
                               (144, "7.5 min"), (108, "10 min")]:
            print("  {:>6s}: {:.0f} days".format(label, life(8, 3, per_day)))
        return

    total = daily()
    part = (DAYTIME_H * 60) * PARTIAL_S * REFRESH_MA / 3600.0
    full = FULLS_DAY * FULL_S * REFRESH_MA / 3600.0
    cpu = (DAYTIME_H * 60) * CPU_S * CPU_MA / 3600.0
    ble = BLE_SYNC_DAY * BLE_SYNC_S * BLE_MA / 3600.0
    pan = DAYTIME_H * PANEL_UA / 1000.0
    print("Daily energy breakdown (typical: "
          "{}mA refresh / {}uA idle):".format(REFRESH_MA, IDLE_UA))
    print("  partial refresh ({}/day)        {:.3f} mAh/day".format(
        int(DAYTIME_H * 60), part))
    print("  full refresh ({}/day)              {:.3f} mAh/day".format(
        FULLS_DAY, full))
    print("  CPU + ADC                              {:.3f} mAh/day".format(cpu))
    print("  BLE re-sync ({}/day)            {:.3f} mAh/day".format(
        BLE_SYNC_DAY, ble))
    print("  panel hibernate ({:.0f}h x {}uA)      {:.3f} mAh/day".format(
        DAYTIME_H, PANEL_UA, pan))
    print("  TOTAL                                {:.3f} mAh/day".format(total))
    print("  -> avg current {:.1f} uA".format(total * 1000 / 24))
    print("  -> battery life {:.0f} days ({:.1f} months) "
          "on {} mAh at {:.0f}% usable".format(
        life(), life() / 30.44, BATTERY_MAH, USABLE * 100))
    print()
    print("  Use --matrix for the full sensitivity range (37-335 days).")


if __name__ == "__main__":
    main()
