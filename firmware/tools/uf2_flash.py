#!/usr/bin/env python3
"""Drop a UF2 onto the XIAO-BOOT drive and verify the bootloader auto-reboots.

Writes a .uf2 to the bootloader's mass-storage drive (D:/FLASH.UF2), then watches
the USB state to confirm the bootloader accepted it and reset into the new
application. The nRF52 UF2 bootloader resets ~1 s after a complete, valid UF2
write (matching familyID); a rejected file (wrong familyID, bad magic, or a
file named CURRENT.UF2) does NOT reset the board.

CAVEAT (verified 2026-08-28): on THIS machine, the bootloader's FAT crashes on
ANY write to the MSC drive — the drive disappears mid-copy and the device drops
off USB (no port, no drive). The canonical UF2 itself is correct (serial DFU of
the same image works and boots), but UF2 drag-and-drop is unreliable here. Use
serial DFU (`pio run -t upload`) as the primary path; this tool is best-effort.

Before copying, backs up D:/CURRENT.UF2 (the bootloader's dump of the ENTIRE
flash: MBR + SoftDevice S140 + bootloader + app) to firmware/backups/ so the
factory state can be restored with a bootloader re-flash if ever needed.

Usage:
    python tools/uf2_flash.py [path-to.uf2]

Defaults to firmware/FIRMWARE.UF2. Seeks the drive by label XIAO-BOOT so it
works even if the drive letter is not D:.

Exit code 0 = file copied AND the application USB identity appeared
(8044/8045) = the bootloader flashed and rebooted into the app.
Exit code 1 = the drive was not present / copy failed / no reboot observed.
"""
import argparse
import os
import shutil
import struct
import sys
import time
from datetime import datetime

import serial
import serial.tools.list_ports

VID = 0x2886
APP_PIDS = (0x8044, 0x8045)   # application CDC (Adafruit core / mbed core)
BOOT_PID = 0x0064             # Seeed UF2 bootloader CDC
DRIVE_LABEL = "XIAO-BOOT"
UF2_MAGIC0 = 0x0A324655


def find_drive():
    """Return the drive root (e.g. 'D:\\') whose volume label is XIAO-BOOT."""
    import string
    for letter in string.ascii_uppercase:
        root = letter + ":\\"
        if os.path.exists(root):
            try:
                import ctypes
                vol = ctypes.create_unicode_buffer(261)
                ctypes.windll.kernel32.GetVolumeInformationW(
                    root, vol, 261, None, None, None, None, 0)
                if vol.value == DRIVE_LABEL:
                    return root
            except Exception:
                pass
    return None


def find_port(pids):
    if isinstance(pids, int):
        pids = [pids]
    for p in serial.tools.list_ports.comports():
        if p.vid == VID and p.pid in pids:
            return p.device
    return None


def is_uf2(path):
    """True if the file begins with the UF2 start magic (either layout)."""
    with open(path, "rb") as f:
        return struct.unpack("<I", f.read(4))[0] == UF2_MAGIC0


def describe_uf2(path):
    """Print a short header summary of a UF2 (works for both header layouts).

    The repo's hex_to_uf2.py writes a 36-byte Python-variant header (familyID
    at byte 32, magic-end at bytes 36-43, payload from byte 44) rather than the
    formal 32-byte spec layout (familyID at byte 24, magic-end at byte 508).
    Both exist in the wild; the bootloader is the arbiter.
    """
    try:
        with open(path, "rb") as f:
            b = f.read(512)
        vals = struct.unpack("<9I", b[0:36])
        print(f"  uf2: start0={vals[0]:08X} start1={vals[1]:08X} "
              f"flags={vals[2]:08X} target={vals[3]:08X} psize={vals[4]} "
              f"blk={vals[5]}/{vals[6]} fsize={vals[7]} fam={vals[8]:08X}")
    except Exception as err:
        print(f"  (could not parse: {err})")


def state_snapshot():
    """(app_port, boot_port, drive_root) tuple of what we see right now."""
    drive = find_drive()
    return (find_port(APP_PIDS), find_port(BOOT_PID), drive)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("uf2", nargs="?", default="FIRMWARE.UF2")
    args = ap.parse_args()

    uf2_path = os.path.abspath(args.uf2)
    if not os.path.isfile(uf2_path):
        print(f"[x] no such file: {uf2_path}")
        return 1
    if not is_uf2(uf2_path):
        print(f"[x] {uf2_path} is not a UF2 (bad magic).")
        return 1
    uf2_size = os.path.getsize(uf2_path)
    print(f"[i] using {uf2_path} ({uf2_size} bytes)")
    describe_uf2(uf2_path)

    drive = find_drive()
    if not drive:
        print("[x] XIAO-BOOT drive not found. Is the board in its bootloader?")
        print("    Run: python tools/reset_to_bootloader.py  (or double-tap RESET)")
        return 1
    print(f"[i] XIAO-BOOT drive at {drive}")

    cur = os.path.join(drive, "CURRENT.UF2")
    if os.path.isfile(cur):
        script_dir = os.path.dirname(os.path.abspath(__file__))
        backup_dir = os.path.join(os.path.dirname(script_dir), "backups")
        os.makedirs(backup_dir, exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        dest = os.path.join(backup_dir, f"CURRENT.UF2.bak-{stamp}")
        shutil.copy2(cur, dest)
        print(f"[i] backed up flash dump -> {dest} "
              f"({os.path.getsize(dest)} bytes)")
    else:
        print("[w] CURRENT.UF2 not present on the drive; skipping backup")

    # ---- copy the UF2 with a forced flush so the write reaches the device ----
    target = os.path.join(drive, "FLASH.UF2")
    try:
        with open(uf2_path, "rb") as src, open(target, "wb") as dst:
            while True:
                chunk = src.read(65536)
                if not chunk:
                    break
                dst.write(chunk)
            dst.flush()
            os.fsync(dst.fileno())
    except OSError as err:
        print(f"[x] copy to {target} failed: {err}")
        return 1
    time.sleep(0.5)
    written = os.path.getsize(target) if os.path.isfile(target) else -1
    print(f"[i] wrote {target} ({written} bytes "
          f"{'OK' if written == uf2_size else 'SIZE MISMATCH'})")

    # ---- watch for the bootloader to accept, reset, and boot the app ----
    print("[i] waiting for the bootloader to flash and auto-reboot...")
    start = time.time()
    last = state_snapshot()
    timeout = 40
    while time.time() - start < timeout:
        now = state_snapshot()
        if now != last:
            t = time.time() - start
            app, boot, drv = now
            print(f"    t+{t:5.1f}s  app={app or '-':<6} boot={boot or '-':<6} "
                  f"drive={drv or '-'}")
            last = now
            if app:
                print("[ok] application CDC appeared — bootloader flashed and "
                      "auto-rebooted into the new firmware.")
                # give the app a moment, then sniff its CDC for any boot text
                time.sleep(2)
                try:
                    with serial.Serial(app, 115200, timeout=2) as s:
                        data = s.read(512)
                    if data:
                        print("[i] app CDC said:\n" + data.decode(errors="replace"))
                except serial.SerialException:
                    pass
                return 0
        time.sleep(0.25)

    print("[x] timeout: no application port appeared.")
    print("    The UF2 was probably rejected (familyID mismatch / bad file).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
