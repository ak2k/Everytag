#!/usr/bin/env python3
# /// script
# requires-python = ">=3.12"
# dependencies = ["bumble", "pytest", "pytest-asyncio"]
# ///
"""
End-to-end GATT tests against the real Everytag firmware on native_sim.

Unlike tests/ble_client/test_conn_beacon.py (which tests a Python re-implementation
of the GATT server), these tests exercise the actual C firmware pipeline:
    gatt_glue.c → beacon_glue_handle_*() → SettingsManager → ZMS

Run:
    1. Build: west build --board native_sim -d build-native-gatt -S tests/native_gatt
    2. Test:  NATIVE_GATT_EXE=./build-native-gatt/zephyr/zephyr.exe pytest tests/native_gatt/ -v
    Or via nix: nix build .#native-gatt-test
"""

import asyncio
import struct

import pytest

from conftest import (
    AUTH_CODE,
    UUID_ACCEL,
    UUID_AIRTAG_FLAG,
    UUID_AUTH,
    UUID_DELAY,
    UUID_FMDN_FLAG,
    UUID_FMDN_KEY,
    UUID_INTERVAL,
    UUID_KEYS,
    UUID_SETTINGS_MAC,
    UUID_STATUS,
    UUID_TIME,
    UUID_TXPOWER,
    authenticate,
    read_char,
    write_char,
)


# ---- Auth enforcement (real write_authorize callback) ----


@pytest.mark.asyncio
async def test_write_before_auth_rejected(firmware_env):
    """Setting writes before auth should fail (real write_authorize in gatt_glue.c)."""
    peer, connection, proc = firmware_env

    with pytest.raises(Exception):
        await write_char(peer, UUID_AIRTAG_FLAG, struct.pack("<i", 1))


@pytest.mark.asyncio
async def test_wrong_auth_then_write_rejected(firmware_env):
    """Wrong auth code should not grant access."""
    peer, connection, proc = firmware_env

    await write_char(peer, UUID_AUTH, b"wrongpwd")
    await asyncio.sleep(0.1)

    with pytest.raises(Exception):
        await write_char(peer, UUID_AIRTAG_FLAG, struct.pack("<i", 1))


@pytest.mark.asyncio
async def test_auth_char_always_writable(firmware_env):
    """Auth characteristic accepts writes even when not authorized."""
    peer, connection, proc = firmware_env

    # Should not raise — auth char is always writable
    await write_char(peer, UUID_AUTH, b"wrongpwd")
    await write_char(peer, UUID_AUTH, AUTH_CODE)


# ---- Authenticated settings writes ----


@pytest.mark.asyncio
async def test_auth_then_enable_airtag(firmware_env):
    """Authenticate, then enable AirTag flag."""
    peer, connection, proc = firmware_env

    await authenticate(peer)
    await write_char(peer, UUID_AIRTAG_FLAG, struct.pack("<i", 1))
    # If we get here without exception, the firmware accepted the write


@pytest.mark.asyncio
async def test_write_tx_power(firmware_env):
    """Write TX power level (0=low, 1=normal, 2=high)."""
    peer, connection, proc = firmware_env

    await authenticate(peer)
    await write_char(peer, UUID_TXPOWER, struct.pack("<i", 2))


@pytest.mark.asyncio
async def test_write_change_interval(firmware_env):
    """Write key change interval (30-7200 seconds)."""
    peer, connection, proc = firmware_env

    await authenticate(peer)
    await write_char(peer, UUID_INTERVAL, struct.pack("<i", 600))


@pytest.mark.asyncio
async def test_write_period(firmware_env):
    """Write advertising period multiplier (allowed: 1, 2, 4, 8)."""
    peer, connection, proc = firmware_env

    await authenticate(peer)
    await write_char(peer, UUID_DELAY, struct.pack("<i", 4))


@pytest.mark.asyncio
async def test_write_fmdn_flag(firmware_env):
    """Enable FMDN broadcasting."""
    peer, connection, proc = firmware_env

    await authenticate(peer)
    await write_char(peer, UUID_FMDN_FLAG, struct.pack("<i", 1))


@pytest.mark.asyncio
async def test_write_fmdn_key(firmware_env):
    """Write 20-byte FMDN key."""
    peer, connection, proc = firmware_env

    await authenticate(peer)
    key = bytes(range(20))
    await write_char(peer, UUID_FMDN_KEY, key)


@pytest.mark.asyncio
async def test_write_settings_mac(firmware_env):
    """Write 6-byte settings MAC address."""
    peer, connection, proc = firmware_env

    await authenticate(peer)
    mac = bytes.fromhex("F5F4F3F2F1F0")
    await write_char(peer, UUID_SETTINGS_MAC, mac)


@pytest.mark.asyncio
async def test_write_status_flags(firmware_env):
    """Write status flags (uint32)."""
    peer, connection, proc = firmware_env

    await authenticate(peer)
    await write_char(peer, UUID_STATUS, struct.pack("<I", 0x438000))


@pytest.mark.asyncio
async def test_write_accel_threshold(firmware_env):
    """Write accelerometer threshold (0-16383)."""
    peer, connection, proc = firmware_env

    await authenticate(peer)
    await write_char(peer, UUID_ACCEL, struct.pack("<i", 800))


# ---- Key upload ----


@pytest.mark.asyncio
async def test_key_upload_two_chunks(firmware_env):
    """Upload one full key (2 × 14-byte chunks)."""
    peer, connection, proc = firmware_env

    await authenticate(peer)

    chunk1 = bytes(range(14))
    chunk2 = bytes(range(14, 28))
    await write_char(peer, UUID_KEYS, chunk1)
    await write_char(peer, UUID_KEYS, chunk2)


# ---- Time offset ----


@pytest.mark.asyncio
async def test_time_write_and_read(firmware_env):
    """Write time offset, read it back."""
    peer, connection, proc = firmware_env

    await authenticate(peer)

    timestamp = 1700000000
    await write_char(peer, UUID_TIME, struct.pack("<q", timestamp))
    await asyncio.sleep(0.1)

    readback = await read_char(peer, UUID_TIME)
    # The firmware stores time_offset = written_time - uptime, then returns
    # time_offset + uptime on read. With non-zero uptime the readback won't
    # be exactly the written value, but should be close (within a few seconds).
    read_time = struct.unpack("<q", readback)[0]
    assert abs(read_time - timestamp) < 10, (
        f"Time drift too large: wrote {timestamp}, read {read_time}"
    )


# ---- All settings in one go ----


@pytest.mark.asyncio
async def test_all_settings(firmware_env):
    """Write every configurable setting in a single session."""
    peer, connection, proc = firmware_env

    await authenticate(peer)

    writes = [
        (UUID_AIRTAG_FLAG, struct.pack("<i", 1)),
        (UUID_FMDN_FLAG, struct.pack("<i", 1)),
        (UUID_DELAY, struct.pack("<i", 2)),
        (UUID_TXPOWER, struct.pack("<i", 1)),
        (UUID_INTERVAL, struct.pack("<i", 300)),
        (UUID_STATUS, struct.pack("<I", 0x438000)),
        (UUID_ACCEL, struct.pack("<i", 400)),
        (UUID_FMDN_KEY, bytes(20)),
        (UUID_SETTINGS_MAC, bytes.fromhex("AABBCCDDEEFF")),
    ]

    for uuid, value in writes:
        await write_char(peer, uuid, value)
        # Small delay between writes to let firmware process each
        await asyncio.sleep(0.05)
