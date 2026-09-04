# Development Setup (Windows 11)

Verified end to end on 2026-08-22: PlatformIO 6.1.19, Python 3.11, Windows 11.

## 1. Install PlatformIO Core

```bash
pip install platformio
pio --version        # expect 6.x
```

VS Code plus the PlatformIO IDE extension is optional; everything below is CLI.

## 2. Build

Build the **mbed** core — the shipped firmware hard-requires it, so the `adafruit` env
will not compile `main.cpp` (it `#error`s unless `ECLOCK_CORE_MBED` is defined):

```bash
cd firmware
pio run -e mbed            # the only env that builds the current firmware (default)
```

The first build downloads the platform and toolchain (a few hundred MB) and takes a
few minutes. Later builds are seconds.

The platform is pinned to `https://github.com/maxgerhardt/platform-nordicnrf52.git`
because the `xiaoble_adafruit` / `xiaoblesense_adafruit` board definitions live in that
fork, not in the official PlatformIO `nordicnrf52` platform.

Current expected footprint (mbed core): RAM 31.5% (74,800 B), Flash 45.4% (368,220 B).

## 3. Identify the two USB identities

The board enumerates as **two different devices with two different COM ports**
depending on what is running. Knowing which is which is essential:

| State | VID:PID | Example port | Notes |
| --- | --- | --- | --- |
| Bootloader | `2886:0064` | COM6 | Accepts serial DFU uploads |
| Application | `2886:8045` | COM10 | TinyUSB CDC from our firmware (mbed core) |

List them at any time with the dedicated helper (also the single source of truth
for the VID/PID table used by `reset_to_bootloader.py` and `capture_boot.py`):

```bash
python tools/find_board.py          # human-readable: role per port + next step
python tools/find_board.py -b       # print only the bootloader port (exit 1 if absent)
python tools/find_board.py -a       # print only the application port (exit 1 if absent)
python tools/find_board.py -m       # machine line: "boot=<port> app=<port>"
```

Your port numbers will differ. Do not assume COM6/COM11.

## 4. Flash

**UF2 Drag-and-Drop (Requires 8.3 filename):**

The bootloader's tiny simulated FAT16 mass storage drive does not fully implement Long File Names (LFN). If you copy a UF2 file with a long name (like `eClock-v0.1.0.uf2`), Windows attempts to write VFAT LFN directory entries, which crashes the bootloader (the drive disconnects mid-transfer, yielding a "device which does not exist" error).

To flash via UF2 drag-and-drop:
1. Double-tap the RESET button to mount the `XIAO-BOOT` drive.
2. **Rename the file to an 8.3-compliant name** (e.g., `ECLOCK.UF2`) *before* copying.
3. Drag and drop it to the drive. The bootloader will reboot into the app automatically.

*(Note: The GitHub Release workflow now automatically produces `ECLOCK.UF2` to avoid this).*

**Serial DFU (the reliable path):**

Two steps:

```bash
cd firmware
python tools/reset_to_bootloader.py                      # prints the bootloader port
pio run -e mbed -t upload --upload-port COM6             # use the port it printed
```

`reset_to_bootloader.py` performs a 1200-baud open/close touch on the application
port, which the bootloader interprets as a reset request. It is idempotent: if the
board is already in the bootloader it says so and exits cleanly.

A successful upload ends with:

```
Activating new firmware
Device programmed.
```

Pass `--upload-port` explicitly. PlatformIO's auto-detection matches on the board
JSON's hwids and can select the application port instead of the bootloader port.

If the script cannot find the board at all, **double-tap the RESET button** to force
the bootloader manually.

## 5. Monitor

```bash
pio device monitor --port COM11 -b 115200
```

Use the **application** port here, not the bootloader port. Expected output:

```
===============================================
  eClock - Phase 1 environment validation
===============================================
  Core       : Adafruit nRF52
  Built      : Aug 22 2026 16:04:34
  Heartbeat  : LED_BLUE @ 1.25 Hz (on=HIGH)
  Panel pins : CS=7 DC=16 RST=11 BUSY=3 PWR=6
-----------------------------------------------
[init] Asserting panel power gate (D6) ... ok, released (panel stays off in Phase 1)

[tick 1] uptime 1s  led=OFF
[tick 2] uptime 2s  led=ON
```

Typing any character echoes it back, which proves the host-to-device path.

To capture the banner itself, the monitor must be attached before the firmware
finishes booting. `tools/capture_boot.py` handles the race by polling for the
application port and opening it the instant it enumerates:

```bash
python tools/reset_to_bootloader.py
# start the capture in one shell...
python tools/capture_boot.py 12
# ...and upload from another
pio run -e mbed -t upload --upload-port COM6
```

## Pitfalls hit during real setup

| Symptom | Cause | Fix |
| --- | --- | --- |
| `'D16' was not declared in this scope` | The variant only defines `D0`–`D10` | Use bare pin indices (`16`, `11`), not `Dxx` macros |
| `undefined reference to 'Serial'`, `Adafruit_USBD_CDC::begin` | On the Adafruit core `Serial` is TinyUSB CDC, and PlatformIO's LDF will not link the bundled library implicitly | `#include <Adafruit_TinyUSB.h>` in the sketch |
| No `XIAO-BOOT` drive after reset | Mass storage does not reliably remount | Use serial DFU (`-t upload`) |
| Upload fails, port busy | A serial monitor is holding the port | Close the monitor first |
| Serial monitor shows nothing | Attached to the bootloader port | Attach to the application port (PID `8045`) |
| Banner missing, only ticks | Joined the stream after boot | Use `tools/capture_boot.py` |
| LED state always identical in tick lines | Blink and report intervals were phase-locked | Blink at 400 ms against a 1000 ms report |
