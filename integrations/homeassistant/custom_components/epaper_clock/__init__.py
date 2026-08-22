import logging
import struct
import time
from datetime import datetime
from bleak import BleakClient
from bleak_retry_connector import establish_connection

from homeassistant.core import HomeAssistant, ServiceCall
from homeassistant.components.bluetooth import (
    BluetoothCallbackMatcher,
    BluetoothScanningMode,
    async_register_callback,
    async_discovered_service_info,
    async_ble_device_from_address,
    BluetoothServiceInfoBleak,
    BluetoothChange,
)

_LOGGER = logging.getLogger(__name__)

DOMAIN = "epaper_clock"
CURRENT_TIME_CHAR_UUID = "00002a2b-0000-1000-8000-00805f9b34fb"
DEVICE_NAME = "ePaper Clock"

async def async_setup(hass: HomeAssistant, config: dict) -> bool:
    """Set up the epaper_clock integration."""
    _LOGGER.info("Setting up ePaper Clock integration")

    last_sync_timestamp = 0.0

    async def async_perform_sync(ble_device, address: str):
        nonlocal last_sync_timestamp
        current_time = time.time()
        
        # 15-second cooldown window to prevent concurrent/redundant sync attempts
        if current_time - last_sync_timestamp < 15.0:
            _LOGGER.debug("Skipping auto-sync for %s (cooldown active)", address)
            return
            
        last_sync_timestamp = current_time
        _LOGGER.info("Attempting connection to ePaper Clock at %s...", address)
        try:
            # Use Home Assistant's standard establish_connection wrapper for reliable connections
            client = await establish_connection(BleakClient, ble_device, address)
            async with client:
                # Fetch local time coordinates
                epoch = int(time.time())
                tz_offset = int(datetime.now().astimezone().utcoffset().total_seconds())
                
                # Pack coordinates: 4-byte little-endian epoch, 4-byte little-endian timezone offset
                payload = struct.pack("<ii", epoch, tz_offset)
                
                _LOGGER.info("Connected! Writing time sync: Epoch=%s, Offset=%s seconds", epoch, tz_offset)
                await client.write_gatt_char(CURRENT_TIME_CHAR_UUID, payload, response=True)
                _LOGGER.info("Time successfully synchronized with ePaper Clock!")
                
        except Exception as err:
            _LOGGER.error("Error communicating with ePaper Clock: %s", err)
            # Reset timestamp on failure so we can retry immediately on next advertisement
            last_sync_timestamp = 0.0

    async def async_sync_time(call: ServiceCall):
        """Sync time with the ePaper clock (manual service trigger)."""
        address = call.data.get("address")
        ble_device = None

        if address:
            ble_device = async_ble_device_from_address(hass, address.upper(), connectable=True)
            if not ble_device:
                _LOGGER.error("Could not find ePaper Clock with address: %s", address)
                return
        else:
            _LOGGER.info("Manual sync triggered without address. Scanning discovered devices...")
            for service_info in async_discovered_service_info(hass, connectable=False):
                _LOGGER.debug("Found device: name=%s, address=%s", service_info.name, service_info.address)
                name = service_info.name or ""
                if "epaper clock" in name.lower():
                    ble_device = async_ble_device_from_address(hass, service_info.address, connectable=True)
                    if ble_device:
                        address = service_info.address
                        _LOGGER.info("Discovered ePaper Clock by name: %s [%s]", service_info.name, address)
                        break
            
            if not ble_device:
                _LOGGER.error("Could not discover ePaper Clock BLE advertisement. Press a button on the clock to start advertising.")
                return

        # Explicit service calls bypass the cooldown check by setting last_sync_timestamp to 0
        nonlocal last_sync_timestamp
        old_sync_ts = last_sync_timestamp
        last_sync_timestamp = 0.0
        await async_perform_sync(ble_device, address)
        if last_sync_timestamp == 0.0:  # If it failed, restore it
            last_sync_timestamp = old_sync_ts

    # Register the manual sync service in Home Assistant
    hass.services.async_register(DOMAIN, "sync_time", async_sync_time)

    # Callback triggered by bluetooth integration on advertisement matching
    def handle_ble_advertisement(
        service_info: BluetoothServiceInfoBleak,
        change: BluetoothChange,
    ) -> None:
        """Handle detected BLE advertisements matching the ePaper Clock."""
        # Filter out stale advertisements (older than 5 seconds)
        adv_age = time.monotonic() - service_info.time
        if adv_age > 5.0:
            _LOGGER.debug(
                "Ignoring stale advertisement from %s (received %.1fs ago)",
                service_info.address,
                adv_age,
            )
            return

        _LOGGER.info(
            "Auto-detected fresh ePaper Clock advertisement from %s [%s] via %s (age: %.2fs). Initiating time sync...",
            service_info.name,
            service_info.address,
            service_info.source,
            adv_age,
        )
        # Schedule the async sync task
        hass.async_create_task(
            async_perform_sync(service_info.device, service_info.address)
        )

    # Register the callback in Home Assistant to monitor advertisements
    matcher = BluetoothCallbackMatcher(
        name="ePaper Clock",
        connectable=True
    )
    
    cancel_callback = async_register_callback(
        hass,
        handle_ble_advertisement,
        matcher,
        BluetoothScanningMode.ACTIVE,
    )
    
    # Store the cancel callback for clean reload/unloading if supported
    hass.data.setdefault(DOMAIN, {})
    hass.data[DOMAIN]["cancel_callback"] = cancel_callback

    return True
