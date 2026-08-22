# eClock Project: Lessons Learned

> **Provenance:** these findings come from hands-on experimentation with this hardware
> prior to this repository being created. They are trustworthy prior art and have
> already saved significant time, but no code currently in this repository exercises
> them. Confirm each as the relevant phase is worked. Pin mappings and board traps are
> also summarised in `docs/hardware/PINOUT.md`.

## Hardware Specifics
### Seeed ePaper Expansion Board (EN04 / EN05) + XIAO nRF52840
*   **Hidden Pogo Pins:** The expansion board uses pogo pins on the bottom of the XIAO to communicate with the display, rather than the standard header pins.
*   **Pin Mappings (CORRECTED 2026-08-22):** The original note below listed these as
    `D7`/`D16`/`D11`/`D3`. **Those macros do not all exist.** The Adafruit variant
    header defines only `D0`-`D10`, so `D16` and `D11` fail to compile. The pin
    *indices* are correct; use bare integers. Verified against
    `framework-arduinoadafruitnrf52-seeed/variants/Seeed_XIAO_nRF52840/`:
    *   `CS`: 7 (P1.12)
    *   `DC`: 16 (P0.27) - also the IMU's I2C SCL
    *   `RST`: 11 (P0.26) - also `LED_RED`
    *   `BUSY`: 3 (P0.29)
*   **Power Control Circuit:** The expansion board features an active power-control
    circuit (MOSFET) connected to **D6** (pin index 6, P1.11). You **must** configure
    `D6` as an output and pull it `HIGH` (`digitalWrite(D6, HIGH)`) to supply power to
    the ePaper screen. If this pin is left floating, the screen receives no power and
    will ignore all SPI commands. *Confirmed drivable without fault in Phase 1; the
    panel itself has not yet been driven.*
*   **Display Panel:** The 2.9" display uses an SSD1680 controller.
*   **Hardware Defects:** If the display is completely failing to update (either ignoring commands entirely or only dimming the screen slightly during an update cycle), it is highly likely a physical hardware defect with the display panel itself rather than a software incompatibility. Replacing the display resolves these issues.

## Software & Library Recommendations

### 0. PlatformIO Toolchain (verified 2026-08-22)
*   **Platform must be the community fork:** The `xiaoble_adafruit` and
    `xiaoblesense_adafruit` board definitions only exist in
    `https://github.com/maxgerhardt/platform-nordicnrf52.git`, not in the official
    PlatformIO `nordicnrf52` platform. Pin the platform by URL.
*   **`Serial` fails to link without TinyUSB:** On the Adafruit core `Serial` is a USB
    CDC endpoint from `Adafruit_TinyUSB`, not a hardware UART. PlatformIO's Library
    Dependency Finder will not link the bundled library implicitly, so a sketch using
    `Serial` compiles fine and then dies at link time with:

    ```
    undefined reference to `Adafruit_USBD_CDC::begin(unsigned long)'
    undefined reference to `Serial'
    ```

    Fix: `#include <Adafruit_TinyUSB.h>`.
*   **Two USB identities:** the board enumerates as VID `0x2886` PID `0x0064` in the
    bootloader and PID `0x8044` when running an application, on *different* COM ports.
    Uploads go to the bootloader port, serial monitoring to the application port.
    Pass `--upload-port` explicitly; auto-detect can pick the wrong one.
*   **UF2 drag-and-drop is not repeatable:** the `XIAO-BOOT` mass-storage volume mounted
    on the first bootloader entry and accepted a `.uf2` correctly, but never remounted on
    later resets (no drive letter, nothing in `Get-Volume`) even though the DFU serial
    port appeared every time. Use `pio run -t upload` (nrfutil serial DFU) instead.
*   **Getting into the bootloader:** a 1200-baud open/close touch on the application
    port works reliably (`firmware/tools/reset_to_bootloader.py`). Double-tapping RESET
    is the manual fallback.
*   **LED polarity:** this variant defines `LED_STATE_ON 1`, i.e. the onboard LEDs are
    active **HIGH**. Derive on/off from `LED_STATE_ON` rather than assuming active low.
*   **Do not blink `LED_RED` in display code:** it is pin index 11 = P0.26 = the ePaper
    RST line. Use `LED_BLUE` (index 12, P0.06).

### 1. `GxEPD2` Library (Highly Recommended)
*   **Pros:** The gold standard for ePaper displays. It fully supports proper hardware clears, intelligent differential partial updates, and double-buffering. It works flawlessly with the SSD1680 controller on the Seeed expansion board once the correct pins and power are configured.
*   **Configuration:** 
    *   Use `GxEPD2_290_T94_V2` (which maps to the SSD1680 controller).
    *   Initialize it with the specific pins: `GxEPD2_BW<GxEPD2_290_T94_V2, GxEPD2_290_T94_V2::HEIGHT> display(GxEPD2_290_T94_V2(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));`
*   **Partial Updates:** To perform lightning-fast partial updates without flashing the screen or causing text ghosting, use `display.setPartialWindow(0, 0, display.width(), display.height())` before running the `firstPage()` / `nextPage()` loop. The library handles the complex double-buffering logic automatically.
*   **Typography:** Adafruit GFX fonts (e.g., `Fonts/FreeSans9pt7b.h`) look significantly better and scale much more cleanly than the default GLCD font.

### 2. Bluetooth (BLE) on Adafruit nRF52 Core (The Bootloader Hard Fault)
*   **`Bluefruit.begin()` Hard Fault:** When compiling for the `xiaoblesense_adafruit` target using the Adafruit nRF52 core, attempting to initialize the BLE stack via `Bluefruit.begin()` results in an immediate CPU hard fault. 
*   **The Root Cause (Incompatible Bootloader):** The crash is NOT caused by the `LFRC` vs `LFXO` crystal setting as originally suspected. The Seeed XIAO nRF52840 ships with the factory Seeed bootloader. The Adafruit `Bluefruit52Lib` and its internal SoftDevice routines strictly expect the custom Adafruit nRF52 UF2 bootloader to be flashed. Attempting to start the Adafruit SoftDevice on the Seeed bootloader causes a memory alignment fault and crashes the chip.
*   **The Solution for Adafruit BLE:** To use the Adafruit core with BLE, you MUST flash the Adafruit nRF52 bootloader onto the XIAO (usually via SWD or dragging a specific UF2 file).

### 3. The `mbed` Core Alternative (The Hardware Peripheral Trap)
*   **Attempting the mbed Core:** To circumvent the bootloader issue without flashing the board, you can switch back to the official Seeed `mbed` core (`board = xiaoble`) and use `ArduinoBLE`.
*   **The EN05 Pogo Pin Trap:** The Adafruit core and the mbed core assign completely different integer aliases to the hidden pogo-pin pads on the bottom of the XIAO that the EN05 expansion board relies on!
    *   In the **Adafruit core**: The `DC` pad is `D16` (`P0.27`) and the `RST` pad is `D11` (`P0.26`).
    *   In the **mbed core**: `D16` doesn't exist, and `P0.27` is actually mapped to `15`. `P0.26` is mapped to `11`.
*   **The Hidden Hardware Lock:** Even if you correctly map the pins to `15` and `11` in the mbed core, the display will **still fail**! This is because `P0.27` and `P0.26` are internally shared with the IMU's I2C bus (`TWIM` hardware peripheral) and the RED LED. The `mbed` core's startup sequence (or the factory bootloader) leaves the hardware I2C peripheral (`NRF_TWIM0`, etc.) enabled on `P0.27`. When a hardware peripheral like I2C is active on an nRF52 pin, it completely overrides normal GPIO toggling! The `GxEPD2` library tries to toggle the DC line, but the hardware I2C controller silently ignores it, leaving the display frozen.
*   **The Solution (The Hardware Hack):** You must add a low-level hardware hack to the start of `setup()` to forcefully disable the I2C peripherals before initializing the display:
    ```cpp
    NRF_TWIM0->ENABLE = 0; NRF_TWIM1->ENABLE = 0;
    NRF_TWI0->ENABLE = 0; NRF_TWI1->ENABLE = 0;
    NRF_GPIO->PIN_CNF[26] = 3; NRF_GPIO->PIN_CNF[27] = 3;
    ```
    This rips control of the pins away from the system and assigns them back to standard GPIO for the screen.
*   **Conclusion:** If you only need the display, stick to the Adafruit core with `D16`/`D11`-it is rock solid. If you need BLE, either flash the Adafruit bootloader, or use the mbed core with `ArduinoBLE`, map the display to `DC = 15` and `RST = 11`, and add the hardware peripheral disable hack to `setup()`.

### 4. `Seeed_GFX` Library (Deprecated for this project)
*   **Pros:** It is the official manufacturer library and contains the precise initialization sequences, but it is heavily flawed for advanced use cases.
*   **Cons (Partial Update & Ghosting Bug):** `Seeed_GFX` is incapable of performing true partial updates out of the box because it fails to push the `OLD_COLORS` buffer to the display hardware. As a result, new text is simply stacked on top of old text, causing severe ghosting and overlapping numbers.
*   **Cons (Deep Sleep Coma):** The library aggressively puts the display into Deep Sleep (`EPD_DEEP_SLEEP()`) after every update. The `epaper.wake()` command is broken on this panel revision, meaning the display becomes unresponsive unless you issue a full hardware reset via `epaper.begin()` before every frame.
*   **Cons (Rotation Bugs):** `Seeed_GFX` fails to properly update viewport bounds when switching to Landscape mode, causing aggressive text wrapping against the left edge. This requires explicitly calling the base class `epaper.TFT_eSPI::setRotation(1)`. Furthermore, `epaper.update()` expects portrait dimensions, requiring a temporary rotation reset to `0` before updating.
*   **The Hack:** It is possible to force `Seeed_GFX` to do partial updates by modifying the library source code to comment out every instance of `sleep()`, which keeps the display awake and preserves its internal RAM so the SSD1680 controller can calculate the delta itself. However, switching to `GxEPD2` is a much cleaner and more reliable solution.
