"""Reset the XIAO nRF52840 from the running application into its bootloader.

Opening the application's USB CDC port at 1200 baud and closing it triggers the
bootloader ("1200-baud touch"). This is the reliable way to get into DFU on this
board -- the UF2 mass-storage drive does not remount dependably after the first
entry, so drag-and-drop flashing is not repeatable.

Usage: python tools/reset_to_bootloader.py
Then:  pio run -e adafruit -t upload --upload-port <bootloader port>
"""
import sys
import time

import serial
import serial.tools.list_ports

VID = 0x2886
APP_PID = 0x8044      # application: TinyUSB CDC
BOOT_PID = 0x0064     # Seeed UF2 bootloader CDC


def find_port(pid):
    for p in serial.tools.list_ports.comports():
        if p.vid == VID and p.pid == pid:
            return p.device
    return None


def main():
    boot = find_port(BOOT_PID)
    if boot:
        print(f"[host] already in bootloader on {boot} - nothing to do")
        print(f"[host] upload with: pio run -e adafruit -t upload --upload-port {boot}")
        return 0

    app = find_port(APP_PID)
    if not app:
        print("[host] FAIL: no XIAO found (neither application nor bootloader).")
        print("[host] Check the cable, or double-tap RESET to force the bootloader.")
        return 1

    print(f"[host] application on {app}, sending 1200-baud touch...")
    try:
        s = serial.Serial()
        s.port = app
        s.baudrate = 1200
        s.timeout = 1
        s.open()
        time.sleep(0.15)
        s.close()
    except serial.SerialException as err:
        # The port vanishing mid-reset is normal and means the touch landed.
        print(f"[host] port closed during reset (expected): {err}")

    deadline = time.time() + 15
    while time.time() < deadline:
        boot = find_port(BOOT_PID)
        if boot:
            print(f"[host] bootloader up on {boot}")
            print(f"[host] upload with: pio run -e adafruit -t upload --upload-port {boot}")
            return 0
        time.sleep(0.1)

    print("[host] FAIL: bootloader did not appear within 15s.")
    print("[host] Fall back to double-tapping the RESET button.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
