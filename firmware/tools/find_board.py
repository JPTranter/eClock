"""Locate the connected XIAO nRF52840 board and report its mode.

The board enumerates as two different USB devices depending on what is running,
so which COM port (and what to do with it) depends on its current mode:

  Bootloader  VID 0x2886 PID 0x0064  -- accepts serial DFU uploads
  Application VID 0x2886 PID 0x8045  -- TinyUSB CDC from our firmware
                         PID 0x8044  -- application (legacy Adafruit core)

This is a convenience script so you never hand-type the VID/PID table. It lists
every attached XIAO with its role and, optionally, a single -m (machine) line
that scripts can parse.

Usage:
  python tools/find_board.py            # human-friendly listing
  python tools/find_board.py -m         # machine-readable: boot=<port> app=<port>
  python tools/find_board.py -b         # print only the bootloader port, exit 1 if absent
  python tools/find_board.py -a         # print only the application port, exit 1 if absent
"""
import argparse
import sys

import serial.tools.list_ports

VID = 0x2886
BOOT_PID = 0x0064                    # Seeed UF2 bootloader CDC
APP_PIDS = (0x8044, 0x8045)          # 0x8044 Adafruit (legacy), 0x8045 mbed (current)


def find_board_ports():
    """Return (bootloader_ports, application_ports) as lists of device names."""
    boot = []
    app = []
    for p in serial.tools.list_ports.comports():
        if p.vid == VID:
            if p.pid == BOOT_PID:
                boot.append(p.device)
            elif p.pid in APP_PIDS:
                app.append(p.device)
    return boot, app


def main(argv=None):
    parser = argparse.ArgumentParser(description="Locate the connected XIAO nRF52840 and report its mode.")
    parser.add_argument("-m", "--machine", action="store_true",
                        help="one machine line: boot=<port> app=<port>")
    parser.add_argument("-b", "--bootloader", action="store_true",
                        help="print only the bootloader port; exit 1 if absent")
    parser.add_argument("-a", "--application", action="store_true",
                        help="print only the application port; exit 1 if absent")
    args = parser.parse_args(argv)

    boot, app = find_board_ports()

    if args.bootloader:
        print(boot[0] if boot else "")
        return 0 if boot else 1
    if args.application:
        print(app[0] if app else "")
        return 0 if app else 1
    if args.machine:
        print(f"boot={boot[0] if boot else ''} app={app[0] if app else ''}")
        return 0 if (boot or app) else 1

    if boot or app:
        print(f"Found {len(boot)} bootloader port(s) and {len(app)} application port(s):")
        for d in boot:
            print(f"  bootloader  {d}   (upload target: pio run -e mbed -t upload --upload-port {d})")
        for d in app:
            print(f"  application {d}")
        if boot and not app:
            print("  -> already in bootloader: ready to upload.")
        elif app and not boot:
            print("  -> running the app: run python tools/reset_to_bootloader.py first.")
    else:
        print("[host] No XIAO found (VID 0x2886). Check the cable, or double-tap RESET to force the bootloader.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
