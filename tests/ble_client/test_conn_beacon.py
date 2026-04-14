#!/usr/bin/env python3
# /// script
# requires-python = ">=3.12"
# dependencies = ["bumble", "pytest", "pytest-asyncio"]
# ///
"""
BLE client tests using Google Bumble virtual transport.
Tests the same GATT operations as conn_beacon.py without real hardware.

Run: uv run tests/ble_client/test_conn_beacon.py
Or:  uv run --with bumble --with pytest --with pytest-asyncio pytest tests/ble_client/
"""
import asyncio
import pytest
import pytest_asyncio
from bumble.controller import Controller
from bumble.device import Device, Peer
from bumble.gatt import Service, Characteristic, Attribute
from bumble.host import Host
from bumble.link import LocalLink
from bumble.hci import Address

# ---- Everytag GATT service definition ----

SERVICE_UUID = "5cfce313-a7e3-45c3-933d-418b8100da7f"

# Characteristic UUIDs (from gatt_glue.c)
UUID_FMDN_FLAG  = "8c5debdb-ad8d-4810-a31f-53862e79ee77"
UUID_AIRTAG_FLAG = "8c5debdc-ad8d-4810-a31f-53862e79ee77"
UUID_DELAY      = "8c5debdd-ad8d-4810-a31f-53862e79ee77"
UUID_KEYS       = "8c5debde-ad8d-4810-a31f-53862e79ee77"
UUID_AUTH       = "8c5debdf-ad8d-4810-a31f-53862e79ee77"
UUID_INTERVAL   = "8c5debe0-ad8d-4810-a31f-53862e79ee77"
UUID_TXPOWER    = "8c5debe1-ad8d-4810-a31f-53862e79ee77"
UUID_FMDN_KEY   = "8c5debe2-ad8d-4810-a31f-53862e79ee77"
UUID_TIME       = "8c5debe3-ad8d-4810-a31f-53862e79ee77"
UUID_SETTINGS_MAC = "8c5debe4-ad8d-4810-a31f-53862e79ee77"
UUID_STATUS     = "8c5debe5-ad8d-4810-a31f-53862e79ee77"
UUID_ACCEL      = "8c5debe6-ad8d-4810-a31f-53862e79ee77"

AUTH_CODE = b"abcdefgh"

WRITABLE = Characteristic.Properties(Characteristic.READABLE | Characteristic.WRITEABLE)
READABLE = Characteristic.Properties(Characteristic.READABLE)
PERMISSIONS = Attribute.Permissions(Attribute.READABLE | Attribute.WRITEABLE)


class BeaconGattServer:
    """Simulates the Everytag beacon's GATT service."""

    def __init__(self):
        self.authorized = False
        self.writes: dict[str, list[bytes]] = {}

    def _on_write(self, uuid: str):
        def handler(connection, value):
            self.writes.setdefault(uuid, []).append(bytes(value))
            if uuid == UUID_AUTH:
                self.authorized = bytes(value) == AUTH_CODE
        return handler

    def make_service(self) -> Service:
        chars = []
        for uuid in [
            UUID_FMDN_FLAG, UUID_AIRTAG_FLAG, UUID_DELAY, UUID_KEYS,
            UUID_AUTH, UUID_INTERVAL, UUID_TXPOWER, UUID_FMDN_KEY,
            UUID_TIME, UUID_SETTINGS_MAC, UUID_STATUS, UUID_ACCEL,
        ]:
            value = AUTH_CODE if uuid == UUID_AUTH else b"\x00\x00\x00\x00"
            char = Characteristic(uuid, WRITABLE, PERMISSIONS, value=bytearray(value))
            char.on("write", self._on_write(uuid))
            chars.append(char)
        return Service(SERVICE_UUID, chars)

    def get_writes(self, uuid: str) -> list[bytes]:
        return self.writes.get(uuid, [])


# ---- Fixtures ----

@pytest_asyncio.fixture
async def beacon_env():
    """Set up two Bumble devices connected via virtual link."""
    link = LocalLink()

    server_ctrl = Controller("server", link=link)
    client_ctrl = Controller("client", link=link)

    server_host = Host(server_ctrl, server_ctrl)
    client_host = Host(client_ctrl, client_ctrl)

    server_addr = Address("F0:F1:F2:F3:F4:F5")
    client_addr = Address("C0:C1:C2:C3:C4:C5")

    server = BeaconGattServer()
    server_device = Device(name="beacon", address=server_addr, host=server_host)
    server_device.add_service(server.make_service())

    client_device = Device(name="client", address=client_addr, host=client_host)

    await server_device.power_on()
    await client_device.power_on()
    await server_device.start_advertising(auto_restart=True)

    # Client connects
    connection = await client_device.connect(server_addr)
    peer = Peer(connection)
    await peer.discover_services()
    await peer.discover_characteristics()

    yield server, peer, connection

    await connection.disconnect()


async def write_char(peer: Peer, uuid: str, value: bytes):
    """Write to a characteristic by UUID."""
    target = uuid.upper()
    for service in peer.services:
        for char in service.characteristics:
            if str(char.uuid).upper() == target:
                await peer.write_value(char, value, with_response=True)
                return
    raise ValueError(f"Characteristic {uuid} not found")


async def read_char(peer: Peer, uuid: str) -> bytes:
    """Read from a characteristic by UUID."""
    target = uuid.upper()
    for service in peer.services:
        for char in service.characteristics:
            if str(char.uuid).upper() == target:
                return await peer.read_value(char)
    raise ValueError(f"Characteristic {uuid} not found")


# ---- Tests ----

@pytest.mark.asyncio
async def test_auth_and_airtag_enable(beacon_env):
    """Auth with correct code, then enable AirTag broadcasting."""
    server, peer, _ = beacon_env

    await write_char(peer, UUID_AUTH, AUTH_CODE)
    await asyncio.sleep(0.05)
    assert server.authorized

    await write_char(peer, UUID_AIRTAG_FLAG, b"\x01\x00\x00\x00")
    await asyncio.sleep(0.05)
    assert server.get_writes(UUID_AIRTAG_FLAG) == [b"\x01\x00\x00\x00"]


@pytest.mark.asyncio
async def test_wrong_auth(beacon_env):
    """Wrong auth code should not authorize."""
    server, peer, _ = beacon_env

    await write_char(peer, UUID_AUTH, b"wrongpwd")
    await asyncio.sleep(0.05)
    assert not server.authorized


@pytest.mark.asyncio
async def test_key_upload(beacon_env):
    """Upload key chunks (same as conn_beacon.py keyfile flow)."""
    server, peer, _ = beacon_env

    await write_char(peer, UUID_AUTH, AUTH_CODE)
    await asyncio.sleep(0.05)

    # Simulate 3 keys (each key = 2 × 14-byte chunks)
    for i in range(6):
        chunk = bytes([i] * 14)
        await write_char(peer, UUID_KEYS, chunk)

    await asyncio.sleep(0.05)
    assert len(server.get_writes(UUID_KEYS)) == 6


@pytest.mark.asyncio
async def test_time_write(beacon_env):
    """Write current time as 8-byte little-endian."""
    server, peer, _ = beacon_env

    await write_char(peer, UUID_AUTH, AUTH_CODE)
    await asyncio.sleep(0.05)

    timestamp = (1700000000).to_bytes(8, byteorder="little")
    await write_char(peer, UUID_TIME, timestamp)
    await asyncio.sleep(0.05)
    assert server.get_writes(UUID_TIME) == [timestamp]


@pytest.mark.asyncio
async def test_all_settings(beacon_env):
    """Set every configurable option (mirrors conn_beacon.py full invocation)."""
    server, peer, _ = beacon_env

    await write_char(peer, UUID_AUTH, AUTH_CODE)
    await asyncio.sleep(0.05)

    settings = {
        UUID_FMDN_FLAG: b"\x01\x00\x00\x00",
        UUID_AIRTAG_FLAG: b"\x01\x00\x00\x00",
        UUID_DELAY: b"\x02\x00\x00\x00",
        UUID_TXPOWER: b"\x02\x00\x00\x00",
        UUID_INTERVAL: (600).to_bytes(4, byteorder="little"),
        UUID_STATUS: (0x438000).to_bytes(4, byteorder="little"),
        UUID_ACCEL: (800).to_bytes(4, byteorder="little"),
        UUID_FMDN_KEY: bytes(20),
        UUID_SETTINGS_MAC: bytes.fromhex("F5F4F3F2F1F0"),
    }

    for uuid, value in settings.items():
        await write_char(peer, uuid, value)

    await asyncio.sleep(0.05)

    for uuid, value in settings.items():
        assert server.get_writes(uuid) == [value], f"Mismatch for {uuid}"


# ---- Run directly ----

if __name__ == "__main__":
    pytest.main([__file__, "-v"])
