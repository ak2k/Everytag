#!/usr/bin/env python3
# /// script
# requires-python = ">=3.12"
# dependencies = ["bumble", "pytest", "pytest-asyncio"]
# ///
"""
Fixtures for native_sim GATT integration tests.

Starts a Bumble virtual BLE controller on a TCP port, launches the native_sim
firmware binary, then provides a connected GATT client for tests.
"""

import asyncio
import os
import signal
from pathlib import Path

import pytest
import pytest_asyncio
from bumble.controller import Controller
from bumble.device import Device, Peer
from bumble.host import Host
from bumble.link import LocalLink
from bumble.transport import open_transport

# ---- Everytag GATT UUIDs (from gatt_glue.c) ----

SERVICE_UUID = "5cfce313-a7e3-45c3-933d-418b8100da7f"

UUID_AUTH = "8c5debdf-ad8d-4810-a31f-53862e79ee77"
UUID_KEYS = "8c5debde-ad8d-4810-a31f-53862e79ee77"
UUID_DELAY = "8c5debdd-ad8d-4810-a31f-53862e79ee77"
UUID_AIRTAG_FLAG = "8c5debdc-ad8d-4810-a31f-53862e79ee77"
UUID_FMDN_FLAG = "8c5debdb-ad8d-4810-a31f-53862e79ee77"
UUID_INTERVAL = "8c5debe0-ad8d-4810-a31f-53862e79ee77"
UUID_TXPOWER = "8c5debe1-ad8d-4810-a31f-53862e79ee77"
UUID_FMDN_KEY = "8c5debe2-ad8d-4810-a31f-53862e79ee77"
UUID_TIME = "8c5debe3-ad8d-4810-a31f-53862e79ee77"
UUID_SETTINGS_MAC = "8c5debe4-ad8d-4810-a31f-53862e79ee77"
UUID_STATUS = "8c5debe5-ad8d-4810-a31f-53862e79ee77"
UUID_ACCEL = "8c5debe6-ad8d-4810-a31f-53862e79ee77"

AUTH_CODE = b"abcdefgh"

# Default firmware binary path (overridable via NATIVE_GATT_EXE env var)
DEFAULT_EXE = Path(__file__).parent / "../../build-native-gatt/zephyr/zephyr.exe"


def _get_free_port() -> int:
    """Find an available TCP port."""
    import socket

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


async def write_char(peer: Peer, uuid: str, value: bytes):
    """Write to a GATT characteristic by UUID."""
    target = uuid.upper()
    for service in peer.services:
        for char in service.characteristics:
            if str(char.uuid).upper() == target:
                await peer.write_value(char, value, with_response=True)
                return
    raise ValueError(f"Characteristic {uuid} not found")


async def read_char(peer: Peer, uuid: str) -> bytes:
    """Read a GATT characteristic by UUID."""
    target = uuid.upper()
    for service in peer.services:
        for char in service.characteristics:
            if str(char.uuid).upper() == target:
                return await peer.read_value(char)
    raise ValueError(f"Characteristic {uuid} not found")


async def authenticate(peer: Peer, auth_code: bytes = AUTH_CODE):
    """Write the auth code and wait for it to take effect."""
    await write_char(peer, UUID_AUTH, auth_code)
    await asyncio.sleep(0.1)


@pytest_asyncio.fixture
async def firmware_env():
    """Start Bumble virtual controller + native_sim firmware, yield connected GATT client.

    Architecture:
        Bumble TCP server (Controller on LocalLink) <-- TCP H4 --> native_sim firmware
        Bumble in-process Device (scanner/client) <-- LocalLink --> same virtual radio
    """
    exe_path = Path(os.environ.get("NATIVE_GATT_EXE", DEFAULT_EXE)).resolve()
    if not exe_path.exists():
        pytest.skip(f"Firmware binary not found: {exe_path}")

    port = _get_free_port()
    link = LocalLink()

    # open_transport("tcp-server:...") creates a TCP server and blocks until
    # a client connects. We need to launch both the server and the firmware
    # concurrently: server starts listening, firmware connects to it.
    async def start_transport():
        return await open_transport(f"tcp-server:_:{port}")

    async def start_firmware():
        # Small delay to let the TCP server start listening
        await asyncio.sleep(0.3)
        return await asyncio.create_subprocess_exec(
            str(exe_path),
            f"--bt-dev=127.0.0.1:{port}",
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )

    transport, fw_proc = await asyncio.wait_for(
        asyncio.gather(start_transport(), start_firmware()),
        timeout=15.0,
    )

    # Controller must stay alive (registers on link via side effect)
    fw_ctrl = Controller(  # noqa: F841
        "FirmwareCtrl",
        host_source=transport.source,
        host_sink=transport.sink,
        link=link,
        public_address="F0:F1:F2:F3:F4:F5",
    )

    # Controller for the test client (in-process)
    client_ctrl = Controller("ClientCtrl", link=link)
    client_host = Host(client_ctrl, client_ctrl)
    client_device = Device(
        name="TestClient",
        address="C0:C1:C2:C3:C4:C5",
        host=client_host,
    )
    await client_device.power_on()

    try:
        # Wait for the firmware to boot and start settings advertising.
        # The state machine starts in Settings mode (connectable) when no keys are loaded.
        # Give it time to init BLE, register GATT, and start advertising.
        await asyncio.sleep(2.0)

        if fw_proc.returncode is not None:
            stdout = await fw_proc.stdout.read()
            stderr = await fw_proc.stderr.read()
            pytest.fail(
                f"Firmware exited early (rc={fw_proc.returncode})\n"
                f"stdout: {stdout.decode(errors='replace')}\n"
                f"stderr: {stderr.decode(errors='replace')}"
            )

        # Scan for the firmware's advertisement
        found_address = None
        adv_event = asyncio.Event()

        def on_advertisement(advertisement):
            nonlocal found_address
            found_address = advertisement.address
            adv_event.set()

        client_device.on("advertisement", on_advertisement)
        await client_device.start_scanning()

        try:
            await asyncio.wait_for(adv_event.wait(), timeout=10.0)
        except asyncio.TimeoutError:
            # Capture firmware output for debugging
            diag = ""
            if fw_proc.returncode is not None:
                stdout = await fw_proc.stdout.read()
                stderr = await fw_proc.stderr.read()
                diag = (
                    f"\nFirmware exited (rc={fw_proc.returncode})"
                    f"\nstdout: {stdout.decode(errors='replace')[:2000]}"
                    f"\nstderr: {stderr.decode(errors='replace')[:2000]}"
                )
            else:
                # Read whatever stdout is available without blocking
                try:
                    partial = await asyncio.wait_for(
                        fw_proc.stdout.read(4096), timeout=0.5
                    )
                    diag = f"\nFirmware is still running. Partial stdout:\n{partial.decode(errors='replace')[:2000]}"
                except asyncio.TimeoutError:
                    diag = "\nFirmware is still running (no stdout captured)"
            pytest.fail(f"Firmware did not start advertising within 10 seconds{diag}")

        await client_device.stop_scanning()

        # Connect to the firmware
        connection = await client_device.connect(found_address)
        peer = Peer(connection)
        await peer.discover_services()
        await peer.discover_characteristics()

        yield peer, connection, fw_proc

    finally:
        # Clean up: terminate firmware process
        if fw_proc.returncode is None:
            fw_proc.send_signal(signal.SIGTERM)
            try:
                await asyncio.wait_for(fw_proc.wait(), timeout=3.0)
            except asyncio.TimeoutError:
                fw_proc.kill()
                await fw_proc.wait()

        await transport.close()
