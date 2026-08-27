# Home Assistant integration — ePaper Clock time sync

A Home Assistant custom component that keeps the eClock's time accurate over BLE.

**Status: working.** This component was written against the firmware in this
repository and validated end-to-end with Home Assistant (Phase 4 signoff: "Working
end-to-end with HA"). The firmware advertises the Current Time Service (`0x1805`) and
exposes a writable `0x2A2B` characteristic; this component discovers the clock and
writes the current epoch + UTC offset.

## How it works

The clock exposes the standard Bluetooth Current Time Service. Home Assistant does the
work of noticing it and pushing the time:

1. The component registers a Bluetooth callback matched on service UUID
   `00001805-...` (Current Time Service) in active scanning mode.
2. When a **fresh** advertisement arrives (anything older than 5 s is ignored), it
   connects using Home Assistant's `establish_connection` wrapper.
3. It writes 8 bytes to characteristic `00002a2b-...`:
   little-endian `int32` Unix epoch, then little-endian `int32` UTC offset in seconds.
   Equivalent to `struct.pack("<ii", epoch, tz_offset)`.
4. A 15-second cooldown prevents redundant or concurrent syncs when the clock
   advertises repeatedly. On failure the cooldown is cleared so the next
   advertisement retries immediately.

## Installation

Copy the component into your Home Assistant config directory:

```
<config>/custom_components/epaper_clock/__init__.py
```

Enable it in `configuration.yaml`. It is **highly recommended** to specify the clock's MAC address (or addresses) to avoid conflicting with other devices that broadcast the same Current Time Service (like Apple Watches):

```yaml
epaper_clock:
  mac_addresses:
    - "AA:BB:CC:DD:EE:FF"
    - "11:22:33:44:55:66"
```

Restart Home Assistant. You will need the Bluetooth integration set up and a working
adapter or ESPHome Bluetooth proxy within range of the clock.

## Manual sync

The component registers a service that bypasses the cooldown:

```yaml
service: epaper_clock.sync_time
```

Optionally target a specific device when several are in range:

```yaml
service: epaper_clock.sync_time
data:
  address: "AA:BB:CC:DD:EE:FF"
```

Without an address it attempts to discover the clock by checking the Bluetooth cache for the advertised name, `ePaper Clock`. **However, name discovery can be flaky** because Home Assistant may cache incomplete profiles (e.g. if it misses the scan response packet). It is highly recommended to provide the MAC address to guarantee immediate discovery.

## Troubleshooting

- *"Could not discover ePaper Clock BLE advertisement"* — the clock only advertises in
  short windows to save power. Press its resync button to force an advertising window,
  then call the service.
- Nothing happens automatically — confirm the firmware advertises service `0x1805`;
  the callback matcher will not fire without it.
- Enable debug logging:

  ```yaml
  logger:
    logs:
      custom_components.epaper_clock: debug
  ```

## Known gaps

- No config flow; YAML-only setup.
- No entities exposed — the clock's battery level and last-sync time are not surfaced
  in Home Assistant. Not yet implemented.
- Unload is only partially handled: the callback canceller is stored in `hass.data`
  but there is no `async_unload_entry`.
