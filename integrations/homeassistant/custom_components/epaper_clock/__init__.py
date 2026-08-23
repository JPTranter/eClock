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

    # Read optional MAC addresses from configuration.yaml
    # Support either a single 'mac_address' string or a list of 'mac_addresses'
    mac_addresses = []
    domain_config = config.get(DOMAIN, {})
    if "mac_address" in domain_config:
        mac_addresses.append(domain_config["mac_address"])
    if "mac_addresses" in domain_config:
        mac_addresses.extend(domain_config["mac_addresses"])
        
    mac_addresses = [mac.upper() for mac in mac_addresses]

    # Dictionary to keep track of cooldowns per MAC address
    last_sync_timestamps = {}

    async def async_perform_sync(ble_device, address: str):
        address_upper = address.upper()
        current_time = time.time()
        last_sync = last_sync_timestamps.get(address_upper, 0.0)
        
        # 15-second cooldown window to prevent concurrent/redundant sync attempts
        if current_time - last_sync < 15.0:
            _LOGGER.debug("Skipping auto-sync for %s (cooldown active)", address)
            return
            
        last_sync_timestamps[address_upper] = current_time
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
                
                # Force Bleak to clear its GATT cache. This is necessary because Home Assistant
                # heavily caches BLE profiles by MAC address. If the board was flashed with a 
                # previous firmware, HA won't discover the new 0x2A2B characteristic without this.
                if hasattr(client, "clear_cache"):
                    await client.clear_cache()
                
                await client.write_gatt_char(CURRENT_TIME_CHAR_UUID, payload, response=True)
                _LOGGER.info("Time successfully synchronized with ePaper Clock %s!", address)
                
        except Exception as err:
            # If we match blindly by UUID, we might connect to another device (e.g. smartwatch).
            # Ignore the 'was not found' error silently in that case so we don't spam logs.
            if "was not found" in str(err) and not mac_addresses:
                _LOGGER.debug("Device %s did not have the characteristic. Likely not the ePaper Clock.", address)
            else:
                _LOGGER.error("Error communicating with ePaper Clock %s: %s", address, err)
            # Reset timestamp on failure so we can retry immediately on next advertisement
            last_sync_timestamps[address_upper] = 0.0

    async def async_sync_time(call: ServiceCall):
        """Sync time with the ePaper clock (manual service trigger)."""
        address = call.data.get("address")
        
        # If no specific address was requested, try to discover based on config
        targets = [address] if address else mac_addresses
        
        if targets:
            for target_mac in targets:
                ble_device = async_ble_device_from_address(hass, target_mac.upper(), connectable=True)
                if not ble_device:
                    _LOGGER.error("Could not find ePaper Clock with address: %s", target_mac)
                    continue
                
                # Explicit service calls bypass the cooldown check
                old_sync_ts = last_sync_timestamps.get(target_mac.upper(), 0.0)
                last_sync_timestamps[target_mac.upper()] = 0.0
                await async_perform_sync(ble_device, target_mac)
                if last_sync_timestamps.get(target_mac.upper(), 0.0) == 0.0:
                    last_sync_timestamps[target_mac.upper()] = old_sync_ts
            return

        # Fallback discovery if no targets are configured or provided
        _LOGGER.info("Manual sync triggered without address. Scanning discovered devices...")
        ble_device = None
        for service_info in async_discovered_service_info(hass, connectable=False):
            _LOGGER.debug("Found device: name=%s, address=%s", service_info.name, service_info.address)
            name = service_info.name or ""
            # Match by name OR by the Current Time Service UUID (0x1805)
            has_uuid = "00001805-0000-1000-8000-00805f9b34fb" in service_info.service_uuids
            if "epaper clock" in name.lower() or has_uuid:
                ble_device = async_ble_device_from_address(hass, service_info.address, connectable=True)
                if ble_device:
                    address = service_info.address
                    _LOGGER.info("Discovered ePaper Clock: %s [%s]", service_info.name, address)
                    break
        
        if not ble_device:
            _LOGGER.error("Could not discover ePaper Clock BLE advertisement. Press a button on the clock to start advertising.")
            return

        old_sync_ts = last_sync_timestamps.get(address.upper(), 0.0)
        last_sync_timestamps[address.upper()] = 0.0
        await async_perform_sync(ble_device, address)
        if last_sync_timestamps.get(address.upper(), 0.0) == 0.0:
            last_sync_timestamps[address.upper()] = old_sync_ts

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

    # Store the cancel callbacks for clean reload/unloading if supported
    hass.data.setdefault(DOMAIN, {})
    cancel_callbacks = []
    
    # Register the callback in Home Assistant to monitor advertisements
    if mac_addresses:
        for mac in mac_addresses:
            matcher = BluetoothCallbackMatcher(
                address=mac.upper(),
                connectable=True
            )
            cb = async_register_callback(hass, handle_ble_advertisement, matcher, BluetoothScanningMode.ACTIVE)
            cancel_callbacks.append(cb)
    else:
        matcher = BluetoothCallbackMatcher(
            service_uuid="00001805-0000-1000-8000-00805f9b34fb",
            connectable=True
        )
        cb = async_register_callback(hass, handle_ble_advertisement, matcher, BluetoothScanningMode.ACTIVE)
        cancel_callbacks.append(cb)
    
    hass.data[DOMAIN]["cancel_callbacks"] = cancel_callbacks

    return True
