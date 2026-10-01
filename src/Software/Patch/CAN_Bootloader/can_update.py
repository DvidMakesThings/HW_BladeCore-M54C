#!/usr/bin/env python3
"""Discover devices, upload firmware over classic CAN, and create SWD factory images.

Configuration is read from this project's root CONFIG.h. The bootloader and
application must use the same protocol constants and application flash offset.
"""
from __future__ import annotations

import argparse
import importlib
import re
import struct
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def configuration() -> dict[str, int]:
    """Read literal numeric protocol settings from the firmware's central header."""
    text = (ROOT / "CONFIG.h").read_text()
    result = {}
    for name, value in re.findall(
        r"^#define\s+(\w+)\s+(0x[0-9a-fA-F]+|[0-9]+)[uUlL]*\b", text, re.MULTILINE
    ):
        result[name] = int(value, 0)
    required = ("CAN_REQUEST", "CAN_RESPONSE", "CAN_IDENTITY", "CAN_DATA",
                "CAN_SELECT", "CAN_ENTER", "APP_OFFSET", "META_OFFSET",
                "PICO_FLASH_SIZE_BYTES", "BOARD_TYPE", "META_MAGIC", "PROTOCOL_VERSION")
    for name in required:
        if name not in result:
            raise ValueError(f"CONFIG.h must define {name} as a numeric literal")
    return result


CFG = configuration()
# Command values are enum members in protocol.h, not preprocessor macros.
COMMANDS = {
    name: int(value)
    for name, value in re.findall(r"(CMD_\w+)\s*=\s*(\d+)",
                                  (ROOT / "incl" / "protocol.h").read_text())
}


def image_bytes(path: Path) -> bytes:
    """Read and validate an application BIN linked for the configured XIP offset."""
    data = path.read_bytes()
    maximum = CFG["PICO_FLASH_SIZE_BYTES"] - CFG["APP_OFFSET"]
    if not 256 <= len(data) <= maximum:
        raise ValueError(f"Application length must be 256..{maximum} bytes")
    stack, reset = struct.unpack_from("<II", data)
    base = 0x10000000 + CFG["APP_OFFSET"]
    if not (0x20000000 < stack <= 0x20082000 and stack % 8 == 0 and reset & 1
            and base <= (reset & ~1) < base + len(data)):
        raise ValueError(f"Invalid application vectors; build the BIN for XIP address 0x{base:08x}")
    return data


def manifest(data: bytes) -> bytes:
    """Build the validity record using the exact unpadded application length."""
    header = struct.pack("<5I", CFG["META_MAGIC"], CFG["PROTOCOL_VERSION"],
                         CFG["BOARD_TYPE"], len(data), zlib.crc32(data))
    return header + struct.pack("<I", zlib.crc32(header))


def pack_image(application: Path, bootloader: Path, output: Path) -> None:
    """Create a combined SWD image without modifying either input binary."""
    data = image_bytes(application)
    loader = bootloader.read_bytes()
    if not loader or len(loader) > CFG["META_OFFSET"]:
        raise ValueError("Bootloader image exceeds its reserved flash region")
    if output.resolve() in (application.resolve(), bootloader.resolve()):
        raise ValueError("Output must not overwrite an input firmware image")
    combined = bytearray(b"\xff" * (CFG["APP_OFFSET"] + len(data)))
    combined[:len(loader)] = loader
    record = manifest(data)
    combined[CFG["META_OFFSET"]:CFG["META_OFFSET"] + len(record)] = record
    combined[CFG["APP_OFFSET"]:] = data
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(combined)
    print(f"Created {output}: {len(combined)} bytes, program at 0x10000000")


def can_module():
    """Load python-can only for hardware commands; pack works with standard Python."""
    try:
        return importlib.import_module("can")
    except ImportError as error:
        raise RuntimeError("Install requirements.txt into your Python environment first") from error


class Device:
    """One selected silicon identity on one CAN bus; transfers are stop-and-wait."""

    def __init__(self, bus, node: int, uid: bytes, timeout: float = 2.0):
        self.bus = bus
        self.node = node
        self.uid = uid
        self.timeout = timeout
        self.transaction = 0
        self.can = can_module()

    def send(self, message_class: int, payload: bytes) -> None:
        """Transmit exactly eight bytes to this device's extended identifier."""
        if len(payload) != 8:
            raise ValueError("Protocol payload must contain eight bytes")
        self.bus.send(self.can.Message(arbitration_id=message_class | self.node,
                                      is_extended_id=True, data=payload), timeout=self.timeout)

    def exchange(self, message_class: int, payload: bytes, command: int,
                 transaction: int = 0, offset: int | None = None) -> bytes:
        """Retry an identical request after lost acknowledgements, up to five times."""
        for attempt in range(5):
            self.send(message_class, payload)
            deadline = time.monotonic() + self.timeout
            while time.monotonic() < deadline:
                frame = self.bus.recv(max(0, deadline - time.monotonic()))
                if frame is None:
                    break
                if (not frame.is_extended_id or frame.is_remote_frame or frame.is_error_frame
                        or frame.arbitration_id != CFG["CAN_RESPONSE"] | self.node
                        or len(frame.data) != 8):
                    continue
                reply = bytes(frame.data)
                if reply[0] != command:
                    continue
                if offset is not None:
                    if struct.unpack_from("<I", reply, 4)[0] != offset:
                        continue
                elif reply[1] != transaction:
                    continue
                if reply[2] != 0:
                    raise RuntimeError(f"Device rejected command {command}, status {reply[2]}, "
                                       f"offset {offset}")
                return reply
            print(f"Retry {attempt + 1}/5: command {command}, offset {offset}")
        raise TimeoutError(f"No acknowledgement from node 0x{self.node:04x}")

    def control(self, name: str, value: int = 0) -> bytes:
        """Issue a transaction-tagged control command with a little-endian word."""
        self.transaction = (self.transaction + 1) & 255
        command = COMMANDS[name]
        payload = struct.pack("<BBI2x", command, self.transaction, value)
        return self.exchange(CFG["CAN_REQUEST"], payload, command, self.transaction)

    def select(self) -> None:
        """Select the complete UID before any destructive command is accepted."""
        self.exchange(CFG["CAN_SELECT"], self.uid, COMMANDS["CMD_SELECT"])

    def enter(self) -> None:
        """Request an application reset into the resident bootloader."""
        # A reset can make its acknowledgement disappear; subsequent discovery
        # establishes whether recovery is actually running before an update starts.
        self.send(CFG["CAN_ENTER"], self.uid)

    def upload(self, data: bytes) -> None:
        """Transfer, verify, and commit a firmware image, leaving it in recovery."""
        self.select()
        self.control("CMD_BEGIN", len(data))
        started = time.monotonic()
        reported = 0
        for offset in range(0, len(data), 4):
            payload = struct.pack("<I", offset) + data[offset:offset + 4].ljust(4, b"\xff")
            self.exchange(CFG["CAN_DATA"], payload, COMMANDS["CMD_DATA"], offset=offset)
            percent = min(100, (offset + 4) * 100 // len(data))
            if percent >= reported + 10:
                print(f"{percent}% ({min(offset + 4, len(data))}/{len(data)} bytes)")
                reported = percent
        self.control("CMD_FINISH", zlib.crc32(data))
        print(f"Flash readback CRC verified and manifest committed in {time.monotonic() - started:.1f}s")


def discover(bus, duration: float = 1.0, node: int = 0) -> dict[int, dict]:
    """Collect full identities and mode responses, refusing observed address collisions."""
    can = can_module()
    transaction = int(time.monotonic() * 1000) & 255
    # Drain stale frames so a previous mode response cannot describe a recent reset.
    for _ in range(1024):
        if bus.recv(0) is None:
            break
    bus.send(can.Message(arbitration_id=CFG["CAN_REQUEST"] | node, is_extended_id=True,
                         data=bytes([COMMANDS["CMD_INFO"], transaction, 0, 0, 0, 0, 0, 0])),
             timeout=1.0)
    result = {}
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        frame = bus.recv(max(0, deadline - time.monotonic()))
        if frame is None:
            break
        if (not frame.is_extended_id or frame.is_remote_frame or frame.is_error_frame
                or len(frame.data) != 8):
            continue
        source = frame.arbitration_id & 0xffff
        message_class = frame.arbitration_id & CFG["CAN_CLASS_MASK"]
        if message_class not in (CFG["CAN_IDENTITY"], CFG["CAN_RESPONSE"]):
            continue
        item = result.setdefault(source, {})
        if message_class == CFG["CAN_IDENTITY"]:
            uid = bytes(frame.data)
            if "uid" in item and item["uid"] != uid:
                raise RuntimeError(f"Address collision at 0x{source:04x}; isolate the intended device")
            item["uid"] = uid
        elif frame.data[0] == COMMANDS["CMD_INFO"] and frame.data[1] == transaction:
            item.update(mode=frame.data[3], version=frame.data[4], valid=bool(frame.data[5]))
    return {address: item for address, item in result.items() if "uid" in item and "mode" in item}


def wait_mode(bus, node: int, uid: bytes, mode: int) -> dict:
    """Require a fresh response with the expected full identity and operating mode."""
    for _ in range(8):
        item = discover(bus, duration=0.5, node=node).get(node)
        if item and item["uid"] == uid and item["mode"] == mode:
            return item
    raise TimeoutError(f"Node 0x{node:04x} did not enter {'bootloader' if mode else 'application'} mode")


def main() -> None:
    """Run the requested local packaging or CAN commissioning operation."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("adapters", "discover", "upload", "pack"))
    parser.add_argument("--interface", default="pcan")
    parser.add_argument("--channel", help="Explicit adapter channel, e.g. PCAN_USBBUS1")
    parser.add_argument("--bitrate", type=int, default=500_000)
    parser.add_argument("--node", type=lambda value: int(value, 0))
    parser.add_argument("--uid", help="Required full sixteen-hex-digit identity for upload")
    parser.add_argument("--application", type=Path)
    parser.add_argument("--bootloader", type=Path, default=ROOT / "build" / "CAN_Bootloader.bin")
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "factory.bin")
    parser.add_argument("--stay", action="store_true", help="Remain in recovery after a successful upload")
    args = parser.parse_args()
    if args.command == "pack":
        if args.application is None:
            parser.error("pack requires --application")
        pack_image(args.application, args.bootloader, args.output)
        return
    can = can_module()
    if args.command == "adapters":
        for adapter in can.detect_available_configs(interfaces=[args.interface]):
            print(adapter)
        return
    if args.channel is None:
        parser.error("Specify --channel; an emulated DUT can also appear as a PCAN adapter")
    if args.command == "upload" and (args.application is None or args.node is None or args.uid is None):
        parser.error("upload requires --application, --node, and --uid from discovery")
    data = image_bytes(args.application) if args.command == "upload" else None
    with can.Bus(interface=args.interface, channel=args.channel, bitrate=args.bitrate) as bus:
        devices = discover(bus)
        for address, item in devices.items():
            print(f"node=0x{address:04x} uid={item['uid'].hex()} "
                  f"mode={'bootloader' if item['mode'] else 'application'} "
                  f"protocol={item['version']} valid={item['valid']}")
        if args.command == "discover":
            if not devices:
                print("No compatible devices responded")
            return
        uid = bytes.fromhex(args.uid)
        if len(uid) != 8 or not 1 <= args.node <= 65534:
            parser.error("UID must be eight bytes and node must be 1..65534")
        item = devices.get(args.node)
        if not item or item["uid"] != uid or item["version"] != CFG["PROTOCOL_VERSION"]:
            raise RuntimeError("Requested identity/protocol did not match discovery; nothing erased")
        device = Device(bus, args.node, uid)
        if item["mode"] != 1:
            device.enter()
            wait_mode(bus, args.node, uid, 1)
        try:
            device.upload(data)
        except (Exception, KeyboardInterrupt):
            # Best-effort abort never commits an incomplete image.
            try:
                device.control("CMD_ABORT")
            except Exception:
                pass
            raise
        if not args.stay:
            try:
                device.control("CMD_BOOT")
            except TimeoutError:
                pass  # Reset may have completed despite a lost final acknowledgement.
            wait_mode(bus, args.node, uid, 0)
            print("Application responded after reboot")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError, TimeoutError) as error:
        raise SystemExit(str(error)) from error
