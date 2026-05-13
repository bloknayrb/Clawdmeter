# /// script
# dependencies = ["bleak>=0.22", "httpx>=0.27"]
# ///
"""
Claude Usage Tracker Daemon (BLE / Windows)

Reads Claude Code OAuth token, polls usage via Anthropic API, sends JSON to
the ESP32 over BLE GATT.  Run with: uv run daemon/claude-usage-daemon.py
"""

import asyncio
import json
import os
import re
import time
from datetime import datetime
from pathlib import Path

import httpx
from bleak import BleakClient, BleakError, BleakScanner
from bleak.backends.characteristic import BleakGATTCharacteristic

DEVICE_NAME   = "Claude Controller"
RX_UUID       = "4c41555a-4465-7669-6365-000000000002"  # host writes usage JSON here
REQ_UUID      = "4c41555a-4465-7669-6365-000000000004"  # device notifies to request a refresh
POLL_INTERVAL = 60   # seconds between Anthropic API polls
TICK          = 5    # inner loop wake interval in seconds

_appdata  = os.environ.get("APPDATA") or str(Path.home() / "AppData" / "Roaming")
MAC_CACHE = Path(_appdata) / "claude-usage-monitor" / "ble-address"
CREDS     = Path.home() / ".claude" / ".credentials.json"

_MAC_RE = re.compile(r"^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$")


def log(msg: str) -> None:
    print(f"[{datetime.now().strftime('%H:%M:%S')}] {msg}", flush=True)


def read_token() -> str:
    data = json.loads(CREDS.read_text())
    return data.get("accessToken") or data["claudeAiOauth"]["accessToken"]


def make_req_handler(event: asyncio.Event):
    """Return a bleak notify callback that sets *event* on any notification."""
    def handler(sender: BleakGATTCharacteristic, data: bytearray) -> None:
        event.set()
    return handler


async def scan_for_device():
    """Scan for DEVICE_NAME, cache its address, return BLEDevice or None."""
    log(f"Scanning for '{DEVICE_NAME}'...")
    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10.0)
    if device:
        MAC_CACHE.parent.mkdir(parents=True, exist_ok=True)
        MAC_CACHE.write_text(device.address)
        log(f"Found: {device.address}")
    return device


async def load_cached_device():
    """Try to resurrect the cached MAC. Re-scans by address to get a fresh
    BLEDevice object — WinRT needs the device actively advertising to connect."""
    if not MAC_CACHE.exists():
        return None
    addr = MAC_CACHE.read_text().strip()
    if not _MAC_RE.match(addr):
        log("Cached MAC is malformed, discarding")
        MAC_CACHE.unlink()
        return None
    log(f"Trying cached address {addr}...")
    device = await BleakScanner.find_device_by_address(addr, timeout=10.0)
    if device is None:
        log("Cached device not advertising, will scan by name")
    return device


async def poll(client: BleakClient, http: httpx.AsyncClient) -> bool:
    """Poll the Anthropic API and write the JSON payload to the RX characteristic."""
    try:
        token = read_token()
    except Exception as e:
        log(f"Error reading token: {e}")
        return False

    now = time.time()
    try:
        r = await http.post(
            "https://api.anthropic.com/v1/messages",
            headers={
                "Authorization": f"Bearer {token}",
                "anthropic-version": "2023-06-01",
                "anthropic-beta": "oauth-2025-04-20",
                "Content-Type": "application/json",
                "User-Agent": "claude-code/2.1.5",
            },
            json={
                "model": "claude-haiku-4-5-20251001",
                "max_tokens": 1,
                "messages": [{"role": "user", "content": "hi"}],
            },
            timeout=30.0,
        )
    except Exception as e:
        log(f"API call failed: {e}")
        return False

    h = r.headers

    def pct(key: str) -> int:
        try:
            return round(float(h.get(key, "0")) * 100)
        except ValueError:
            return 0

    def reset_mins(key: str) -> int:
        try:
            return max(0, round((float(h.get(key, "0")) - now) / 60))
        except ValueError:
            return 0

    payload = json.dumps({
        "s":  pct("anthropic-ratelimit-unified-5h-utilization"),
        "sr": reset_mins("anthropic-ratelimit-unified-5h-reset"),
        "w":  pct("anthropic-ratelimit-unified-7d-utilization"),
        "wr": reset_mins("anthropic-ratelimit-unified-7d-reset"),
        "st": h.get("anthropic-ratelimit-unified-5h-status", "unknown"),
        "ok": True,
    })
    log(f"Sending: {payload}")

    try:
        await client.write_gatt_char(RX_UUID, payload.encode(), response=True)
    except BleakError as e:
        log(f"Write failed: {e}")
        return False
    return True


async def run() -> None:
    backoff = 1

    async with httpx.AsyncClient() as http:
        while True:
            device = await load_cached_device() or await scan_for_device()

            if device is None:
                log(f"Device not found, retrying in {backoff}s...")
                await asyncio.sleep(backoff)
                backoff = min(backoff * 2, 60)
                continue

            refresh_event    = asyncio.Event()
            disconnect_event = asyncio.Event()

            try:
                async with BleakClient(
                    device,
                    disconnected_callback=lambda _: disconnect_event.set(),
                ) as client:
                    log(f"Connected to {device.address}")
                    backoff = 1

                    try:
                        await client.start_notify(REQ_UUID, make_req_handler(refresh_event))
                    except BleakError as e:
                        log(f"Could not subscribe to REQ char: {e}")

                    await poll(client, http)
                    last_poll = time.time()

                    while True:
                        try:
                            await asyncio.wait_for(refresh_event.wait(), timeout=TICK)
                        except asyncio.TimeoutError:
                            pass

                        if disconnect_event.is_set():
                            break

                        now = time.time()
                        if refresh_event.is_set():
                            refresh_event.clear()
                            log("Refresh requested by device")
                            if await poll(client, http):
                                last_poll = now
                        elif now - last_poll >= POLL_INTERVAL:
                            if await poll(client, http):
                                last_poll = now

            except BleakError as e:
                log(f"Connection error: {e}")
                # Invalidate cached MAC so next iteration scans by name rather
                # than retrying a dead address (mirrors bash's bluetoothctl remove).
                if MAC_CACHE.exists():
                    MAC_CACHE.unlink()
                    log("Invalidated cached MAC, will rescan by name")

            log("Disconnected, reconnecting...")
            await asyncio.sleep(2)


def main() -> None:
    log("=== Claude Usage Tracker Daemon (BLE / Windows) ===")
    log(f"Poll interval: {POLL_INTERVAL}s")
    try:
        asyncio.run(run())
    except KeyboardInterrupt:
        log("Daemon stopped")


if __name__ == "__main__":
    main()
