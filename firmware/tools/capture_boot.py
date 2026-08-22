"""Capture the eClock boot banner.

The banner prints once at startup. USB CDC enumeration takes ~1-2s after reset,
and the firmware only waits SERIAL_WAIT_MS (3s) for a host before printing
regardless, so there is a narrow window. Poll hard for the port and open it the
moment it appears.

Usage: python capture_boot.py [seconds]
"""
import sys
import time

import serial
import serial.tools.list_ports

APP_PID = 0x8044      # application (TinyUSB CDC)
BOOT_PID = 0x0064     # Seeed UF2 bootloader
VID = 0x2886


def find_port(pid):
    for p in serial.tools.list_ports.comports():
        if p.vid == VID and p.pid == pid:
            return p.device
    return None


def main():
    duration = float(sys.argv[1]) if len(sys.argv) > 1 else 20.0

    print("[host] waiting for application port (VID 2886 PID 8044)...")
    deadline = time.time() + 30
    port = None
    while time.time() < deadline:
        port = find_port(APP_PID)
        if port:
            break
        time.sleep(0.02)   # poll fast to win the enumeration race

    if not port:
        boot = find_port(BOOT_PID)
        if boot:
            print(f"[host] FAIL: board is in BOOTLOADER mode on {boot}, not running an app.")
        else:
            print("[host] FAIL: no XIAO found at all.")
        return 1

    print(f"[host] found {port}, opening immediately")

    s = None
    for attempt in range(40):
        try:
            s = serial.Serial(port, 115200, timeout=0.5)
            break
        except serial.SerialException:
            time.sleep(0.05)

    if s is None:
        print("[host] FAIL: port appeared but could not be opened.")
        return 1

    print(f"[host] open. capturing {duration:.0f}s")
    print("-" * 60)

    sent = False
    end = time.time() + duration
    while time.time() < end:
        line = s.readline()
        if line:
            sys.stdout.write(line.decode("utf-8", "replace").replace("\r", ""))
            sys.stdout.flush()
            if not sent and b"tick 2]" in line:
                s.write(b"Q")
                s.flush()
                sent = True

    s.close()
    print("-" * 60)
    print("[host] capture complete")
    return 0


if __name__ == "__main__":
    sys.exit(main())
