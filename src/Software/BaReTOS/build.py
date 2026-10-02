#!/usr/bin/env python3
"""
BaReTOS Firmware Build and Upload Script
=================================================
Universal script, works on any PC regardless of installation paths.
Requires Python 3. Invoke as `python build.py` or `python3 build.py`.

Usage:
    python build.py              # configure if needed, then build
    python build.py clean        # delete build directory
    python build.py rebuild      # clean, configure, build
    python build.py upload       # build, then flash via picotool (BOOTSEL)
    python build.py swd          # build, then flash the app via SWD (CMSIS-DAP / OpenOCD),
                                 # bootloader region is left untouched
    python build.py size         # show memory usage of last build
    python build.py adapters     # list detected CAN adapters
    python build.py discover     # auto-detect adapter, list responding boards
    python build.py can          # build, auto-detect adapter + board, upload over CAN
                                 # (.hex/.bin; prompts to choose when several are found)
"""
import os
import re
import sys
import glob
import time
import zlib
import struct
import shutil
import argparse
import platform
import importlib
import subprocess
from pathlib import Path

PROJECT_NAME = "BaReTOS"

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR  = os.path.join(SCRIPT_DIR, "build")
UF2_FILE   = os.path.join(BUILD_DIR, PROJECT_NAME + ".uf2")
ELF_FILE   = os.path.join(BUILD_DIR, PROJECT_NAME + ".elf")
HEX_FILE   = os.path.join(BUILD_DIR, PROJECT_NAME + ".hex")
BIN_FILE   = os.path.join(BUILD_DIR, PROJECT_NAME + ".bin")

CONFIG_H   = os.path.join(SCRIPT_DIR, "CONFIG.h")
PROTOCOL_H = os.path.join(SCRIPT_DIR, "incl", "protocol", "protocol.h")

if platform.system() == "Windows":
    _USER_HOME = os.environ.get("USERPROFILE", "")
else:
    _USER_HOME = os.environ.get("HOME", "")

_PICO_SDK_BASE = os.path.join(_USER_HOME, ".pico-sdk")


def _find_tool(name, *fallback_paths):
    found = shutil.which(name)
    if found:
        return found
    for p in fallback_paths:
        if os.path.isfile(p):
            return p
    return None


def _newest_glob(pattern):
    matches = sorted(glob.glob(pattern))
    return matches[-1] if matches else None


def find_cmake():
    tool = _find_tool(
        "cmake",
        os.path.join(_PICO_SDK_BASE, "cmake", "v3.31.5", "bin", "cmake.exe"),
        r"C:\Program Files\CMake\bin\cmake.exe",
        r"C:\Program Files (x86)\CMake\bin\cmake.exe",
    )
    if not tool:
        tool = _newest_glob(os.path.join(_PICO_SDK_BASE, "cmake", "*", "bin", "cmake*"))
    if not tool:
        sys.exit("[ERROR] cmake not found.")
    return tool


def find_ninja():
    tool = _find_tool(
        "ninja",
        os.path.join(_PICO_SDK_BASE, "ninja", "v1.12.1", "ninja.exe"),
    )
    if not tool:
        tool = _newest_glob(os.path.join(_PICO_SDK_BASE, "ninja", "*", "ninja*"))
    if not tool:
        sys.exit("[ERROR] ninja not found.")
    return tool


def find_picotool():
    tool = _find_tool("picotool")
    if tool:
        return tool
    ext = ".exe" if platform.system() == "Windows" else ""
    tool = _newest_glob(os.path.join(_PICO_SDK_BASE, "picotool", "*", "picotool", "picotool" + ext))
    if tool:
        return tool
    sys.exit("[ERROR] picotool not found.")


def find_openocd():
    ext = ".exe" if platform.system() == "Windows" else ""
    # Prefer the Pico SDK OpenOCD: it ships the RP2350 target scripts that stock
    # builds (e.g. Chocolatey) on PATH usually lack.
    exe = _newest_glob(os.path.join(_PICO_SDK_BASE, "openocd", "*", "openocd" + ext)) \
        or _find_tool("openocd")
    if not exe:
        sys.exit("[ERROR] openocd not found.")
    scripts = os.path.join(os.path.dirname(exe), "scripts")
    return exe, scripts


def _find_toolchain_bin():
    return _newest_glob(os.path.join(_PICO_SDK_BASE, "toolchain", "*", "bin"))


def find_size():
    tool = _find_tool("arm-none-eabi-size")
    if tool:
        return tool
    tc_bin = _find_toolchain_bin()
    if tc_bin:
        for name in ("arm-none-eabi-size.exe", "arm-none-eabi-size"):
            p = os.path.join(tc_bin, name)
            if os.path.isfile(p):
                return p
    return None


def find_pico_sdk():
    env = os.environ.get("PICO_SDK_PATH", "")
    if env and os.path.isdir(env):
        return env
    found = _newest_glob(os.path.join(_PICO_SDK_BASE, "sdk", "*"))
    if found and os.path.isdir(found):
        return found
    sys.exit("[ERROR] Pico SDK not found. Set PICO_SDK_PATH.")


def find_pico_toolchain():
    env = os.environ.get("PICO_TOOLCHAIN_PATH", "")
    if env and os.path.isdir(env):
        return env
    found = _newest_glob(os.path.join(_PICO_SDK_BASE, "toolchain", "*"))
    if found and os.path.isdir(found):
        return found
    return None


def _run(cmd, env=None):
    print(f"[RUN] {' '.join(str(c) for c in cmd)}")
    result = subprocess.run(cmd, env=env)
    if result.returncode != 0:
        sys.exit(f"[ERROR] Command failed with exit code {result.returncode}")


def do_clean():
    if os.path.isdir(BUILD_DIR):
        print(f"[*] Removing {BUILD_DIR}")
        shutil.rmtree(BUILD_DIR)
    else:
        print("[*] Build directory does not exist.")


def do_configure():
    cmake = find_cmake()
    ninja = find_ninja()
    sdk   = find_pico_sdk()
    tc    = find_pico_toolchain()

    os.makedirs(BUILD_DIR, exist_ok=True)

    env = os.environ.copy()
    env["PICO_SDK_PATH"] = sdk
    if tc:
        env["PICO_TOOLCHAIN_PATH"] = tc
        tc_bin = os.path.join(tc, "bin")
        if tc_bin not in env.get("PATH", ""):
            env["PATH"] = tc_bin + os.pathsep + env.get("PATH", "")

    cmd = [
        cmake,
        "-G", "Ninja",
        f"-DCMAKE_MAKE_PROGRAM={ninja}",
        f"-DPICO_SDK_PATH={sdk}",
        "-B", BUILD_DIR,
        "-S", SCRIPT_DIR,
    ]
    print("[*] Configuring CMake...")
    _run(cmd, env=env)


def do_build():
    cmake = find_cmake()
    if not os.path.isfile(os.path.join(BUILD_DIR, "CMakeCache.txt")):
        do_configure()
    print("[*] Building firmware...")
    _run([cmake, "--build", BUILD_DIR, "--", "-j4"])
    print("\n[+] BUILD SUCCESSFUL")
    do_size()


def do_size():
    size = find_size()
    if size and os.path.isfile(ELF_FILE):
        print("\n=== Memory Usage ===")
        subprocess.run([size, ELF_FILE])
        print("\n=== Output Files ===")
        for f in (UF2_FILE, HEX_FILE, BIN_FILE, ELF_FILE):
            if os.path.isfile(f):
                print(f"   {os.path.basename(f)}  -  {os.path.getsize(f):,} bytes")
    elif not os.path.isfile(ELF_FILE):
        print("[!] No ELF found. Run build first.")


def do_upload():
    if not os.path.isfile(UF2_FILE):
        print("[*] UF2 not found, building first.")
        do_build()

    picotool = find_picotool()
    print("\n[*] Flashing via picotool (put board in BOOTSEL mode)...")
    _run([picotool, "load", UF2_FILE, "-f"])
    print("[*] Rebooting board...")
    _run([picotool, "reboot"])
    print("\n[+] FLASH SUCCESSFUL")


def _build_manifest_page(cfg, app):
    # Mirror image_manifest_t and the commit path in CAN_Bootloader/incl/update.c.
    header = struct.pack("<5I", cfg["META_MAGIC"], cfg["PROTOCOL_VERSION"],
                         cfg["BOARD_TYPE"], len(app), zlib.crc32(app) & 0xFFFFFFFF)
    manifest = header + struct.pack("<I", zlib.crc32(header) & 0xFFFFFFFF)
    return manifest + b"\xff" * (256 - len(manifest))


def do_swd():
    # ELF is linked at APP_BASE (0x10000000 + APP_OFFSET), so OpenOCD only
    # writes the application region and the bootloader stays intact. A valid
    # image_manifest_t is also written to META_OFFSET so the bootloader
    # accepts the image and boots the app on the next reset.
    if not os.path.isfile(ELF_FILE):
        print("[*] ELF not found, building first.")
        do_build()
    if not os.path.isfile(BIN_FILE):
        sys.exit("[ERROR] BIN not produced by build; cannot generate manifest.")
    cfg = _defines(CONFIG_H)
    required = ("META_OFFSET", "META_MAGIC", "PROTOCOL_VERSION", "BOARD_TYPE")
    missing = [name for name in required if name not in cfg]
    if missing:
        sys.exit(f"[ERROR] CONFIG.h missing numeric defines: {', '.join(missing)}")
    app = Path(BIN_FILE).read_bytes()
    page = _build_manifest_page(cfg, app)
    manifest_path = os.path.join(BUILD_DIR, "manifest.bin")
    Path(manifest_path).write_bytes(page)
    meta_addr = 0x10000000 + cfg["META_OFFSET"]

    openocd, scripts = find_openocd()
    elf = ELF_FILE.replace("\\", "/")
    manifest = manifest_path.replace("\\", "/")
    print("\n[*] Flashing application via SWD (CMSIS-DAP / OpenOCD); bootloader preserved...")
    _run([
        openocd, "-s", scripts,
        "-f", "interface/cmsis-dap.cfg",
        "-f", "target/rp2350.cfg",
        "-c", "adapter speed 5000",
        "-c", "init",
        "-c", "reset halt",
        "-c", f"program {elf} verify",
        "-c", f"program {manifest} 0x{meta_addr:08x} verify",
        "-c", "reset run",
        "-c", "exit",
    ])
    print("\n[+] SWD FLASH SUCCESSFUL")


# ---------------------------------------------------------------------------
# CAN bootloader upload (classic CAN, stop-and-wait; accepts Intel HEX or BIN)
# ---------------------------------------------------------------------------
# The application runs on 11-bit standard CAN frames. The bootloader is a
# separate project kept at the historical 29-bit extended wire protocol; its
# identifiers are listed here so this script needs no cross-project file paths.
CFG = None
COMMANDS = None

_APP_CFG = {
    "CAN_REQUEST":    0x080,
    "CAN_RESPONSE":   0x100,
    "CAN_IDENTITY":   0x180,
    "CAN_DATA":       0x200,
    "CAN_SELECT":     0x280,
    "CAN_ENTER":      0x300,
    "CAN_CLASS_MASK": 0x780,
    "CAN_NODE_MASK":  0x07f,
}

_BOOT_CFG = {
    "CAN_REQUEST":    0x18b00000,
    "CAN_RESPONSE":   0x18b10000,
    "CAN_IDENTITY":   0x18b20000,
    "CAN_DATA":       0x18b30000,
    "CAN_SELECT":     0x18b40000,
    "CAN_ENTER":      0x18b50000,
    "CAN_CLASS_MASK": 0x1fff0000,
    "CAN_NODE_MASK":  0x0000ffff,
}


def _defines(path):
    text = Path(path).read_text()
    result = {}
    for name, value in re.findall(
        r"^#define\s+(\w+)\s+(0x[0-9a-fA-F]+|[0-9]+)[uUlL]*\b", text, re.MULTILINE
    ):
        result[name] = int(value, 0)
    return result


def _can_config():
    cfg = _defines(CONFIG_H)
    required = ("APP_OFFSET", "PICO_FLASH_SIZE_BYTES", "PROTOCOL_VERSION")
    missing = [name for name in required if name not in cfg]
    if missing:
        sys.exit(f"[ERROR] CONFIG.h missing numeric defines: {', '.join(missing)}")
    cfg.update(_APP_CFG)
    return cfg


def _can_commands():
    text = Path(PROTOCOL_H).read_text()
    commands = {name: int(value)
                for name, value in re.findall(r"(CMD_\w+)\s*=\s*(\d+)", text)}
    if not commands:
        sys.exit(f"[ERROR] No CMD_* enums found in {PROTOCOL_H}")
    return commands


def _parse_intel_hex(text):
    """Return (base_address, bytes) from Intel HEX, padding address gaps with 0xFF."""
    cells = {}
    base = 0
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line:
            continue
        if not line.startswith(":"):
            raise ValueError(f"Intel HEX line {lineno}: missing ':' start code")
        rec = bytes.fromhex(line[1:])
        count = rec[0]
        if len(rec) != count + 5:
            raise ValueError(f"Intel HEX line {lineno}: length mismatch")
        if (sum(rec) & 0xFF) != 0:
            raise ValueError(f"Intel HEX line {lineno}: checksum error")
        offset = (rec[1] << 8) | rec[2]
        rtype = rec[3]
        payload = rec[4:4 + count]
        if rtype == 0x00:
            for i, byte in enumerate(payload):
                cells[base + offset + i] = byte
        elif rtype == 0x01:
            break
        elif rtype == 0x04:
            base = ((payload[0] << 8) | payload[1]) << 16
        elif rtype == 0x02:
            base = ((payload[0] << 8) | payload[1]) << 4
        elif rtype == 0x05:
            continue
        else:
            raise ValueError(f"Intel HEX line {lineno}: unsupported record type {rtype:#04x}")
    if not cells:
        raise ValueError("Intel HEX file contained no program data")
    low, high = min(cells), max(cells)
    image = bytearray(b"\xff" * (high - low + 1))
    for addr, byte in cells.items():
        image[addr - low] = byte
    return low, bytes(image)


def load_application(path):
    """Load and validate an application image (.hex or .bin) linked for the app XIP base."""
    cfg = _can_config()
    app_base = 0x10000000 + cfg["APP_OFFSET"]
    maximum = cfg["PICO_FLASH_SIZE_BYTES"] - cfg["APP_OFFSET"]
    if str(path).lower().endswith(".hex"):
        base, data = _parse_intel_hex(Path(path).read_text())
        if base != app_base:
            raise ValueError(
                f"HEX starts at 0x{base:08x}, expected application base 0x{app_base:08x}")
    else:
        data = Path(path).read_bytes()
    if not 256 <= len(data) <= maximum:
        raise ValueError(f"Application length must be 256..{maximum} bytes")
    stack, reset = struct.unpack_from("<II", data)
    if not (0x20000000 < stack <= 0x20082000 and stack % 8 == 0 and reset & 1
            and app_base <= (reset & ~1) < app_base + len(data)):
        raise ValueError(f"Invalid application vectors; build the image for XIP address 0x{app_base:08x}")
    return data


def _can_module():
    try:
        return importlib.import_module("can")
    except ImportError:
        sys.exit("[ERROR] python-can not installed. Run: python -m pip install -r requirements.txt")


class _Device:
    """One selected silicon identity talking to the bootloader; stop-and-wait."""

    def __init__(self, bus, node, uid, cfg=_BOOT_CFG, timeout=2.0):
        self.bus = bus
        self.node = node
        self.uid = uid
        self.cfg = cfg
        self.timeout = timeout
        self.transaction = 0
        self.can = _can_module()

    def send(self, message_class, payload):
        if len(payload) != 8:
            raise ValueError("Protocol payload must contain eight bytes")
        self.bus.send(self.can.Message(arbitration_id=message_class | self.node,
                                       is_extended_id=True, data=payload), timeout=self.timeout)

    def exchange(self, message_class, payload, command, transaction=0, offset=None):
        for attempt in range(5):
            self.send(message_class, payload)
            deadline = time.monotonic() + self.timeout
            while time.monotonic() < deadline:
                frame = self.bus.recv(max(0, deadline - time.monotonic()))
                if frame is None:
                    break
                if (not frame.is_extended_id or frame.is_remote_frame or frame.is_error_frame
                        or frame.arbitration_id != self.cfg["CAN_RESPONSE"] | self.node
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
                    raise RuntimeError(f"Device rejected command {command}, status {reply[2]}, offset {offset}")
                return reply
            print(f"Retry {attempt + 1}/5: command {command}, offset {offset}")
        raise TimeoutError(f"No acknowledgement from node 0x{self.node:04x}")

    def control(self, name, value=0):
        self.transaction = (self.transaction + 1) & 255
        command = COMMANDS[name]
        payload = struct.pack("<BBI2x", command, self.transaction, value)
        return self.exchange(self.cfg["CAN_REQUEST"], payload, command, self.transaction)

    def select(self):
        self.exchange(self.cfg["CAN_SELECT"], self.uid, COMMANDS["CMD_SELECT"])

    def upload(self, data):
        self.select()
        self.control("CMD_BEGIN", len(data))
        started = time.monotonic()
        reported = 0
        for offset in range(0, len(data), 4):
            payload = struct.pack("<I", offset) + data[offset:offset + 4].ljust(4, b"\xff")
            self.exchange(self.cfg["CAN_DATA"], payload, COMMANDS["CMD_DATA"], offset=offset)
            percent = min(100, (offset + 4) * 100 // len(data))
            if percent >= reported + 10:
                print(f"{percent}% ({min(offset + 4, len(data))}/{len(data)} bytes)")
                reported = percent
        self.control("CMD_FINISH", zlib.crc32(data))
        print(f"Flash readback CRC verified and manifest committed in {time.monotonic() - started:.1f}s")


def _trigger_enter(bus, node, uid, cfg, timeout=2.0):
    """Send CMD_ENTER as a standard frame to the running application."""
    bus.send(_can_module().Message(
        arbitration_id=cfg["CAN_ENTER"] | (node & cfg["CAN_NODE_MASK"]),
        is_extended_id=False, data=uid), timeout=timeout)


def _discover(bus, cfg, extended, duration=1.0, node=0):
    can = _can_module()
    node_mask = cfg["CAN_NODE_MASK"]
    transaction = int(time.monotonic() * 1000) & 255
    for _ in range(1024):
        if bus.recv(0) is None:
            break
    bus.send(can.Message(arbitration_id=cfg["CAN_REQUEST"] | (node & node_mask),
                         is_extended_id=extended,
                         data=bytes([COMMANDS["CMD_INFO"], transaction, 0, 0, 0, 0, 0, 0])),
             timeout=1.0)
    result = {}
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        frame = bus.recv(max(0, deadline - time.monotonic()))
        if frame is None:
            break
        if (frame.is_extended_id != extended or frame.is_remote_frame or frame.is_error_frame
                or len(frame.data) != 8):
            continue
        source = frame.arbitration_id & node_mask
        message_class = frame.arbitration_id & cfg["CAN_CLASS_MASK"]
        if message_class not in (cfg["CAN_IDENTITY"], cfg["CAN_RESPONSE"]):
            continue
        item = result.setdefault(source, {})
        if message_class == cfg["CAN_IDENTITY"]:
            uid = bytes(frame.data)
            if "uid" in item and item["uid"] != uid:
                raise RuntimeError(f"Address collision at 0x{source:04x}; isolate the intended device")
            item["uid"] = uid
        elif frame.data[0] == COMMANDS["CMD_INFO"] and frame.data[1] == transaction:
            item.update(mode=frame.data[3], version=frame.data[4], valid=bool(frame.data[5]))
    return {address: item for address, item in result.items() if "uid" in item and "mode" in item}


def _wait_mode(bus, uid, mode, cfg, extended, attempts=8, settle=0.0):
    # Match by UID: the visible node value changes with CAN_NODE_MASK between the
    # standard app (7-bit) and the extended bootloader (16-bit).
    if settle:
        time.sleep(settle)
    for _ in range(attempts):
        for address, item in _discover(bus, cfg=cfg, extended=extended, duration=0.5).items():
            if item["uid"] == uid and item["mode"] == mode:
                return address, item
    raise TimeoutError(f"Node uid={uid.hex()} did not enter {'bootloader' if mode else 'application'} mode")


def _discover_any(bus, duration=1.0):
    """Standard app first, then extended bootloader. Returns the device dict."""
    devices = _discover(bus, cfg=CFG, extended=False, duration=duration)
    if devices:
        return devices
    return _discover(bus, cfg=_BOOT_CFG, extended=True, duration=duration)


def _print_devices(devices):
    for address, item in devices.items():
        print(f"node=0x{address:04x} uid={item['uid'].hex()} "
              f"mode={'bootloader' if item['mode'] else 'application'} "
              f"protocol={item['version']} valid={item['valid']}")


def _open_bus(args):
    return _can_module().Bus(interface=args.interface, channel=args.channel, bitrate=args.bitrate)


def _choose(count, prompt="Select"):
    """Prompt for a 1-based choice and return the 0-based index."""
    while True:
        reply = input(f"{prompt} [1-{count}]: ").strip()
        if reply.isdigit() and 1 <= int(reply) <= count:
            return int(reply) - 1
        print("Invalid selection.")


def _select_channel(args):
    """Return a CAN channel, auto-detecting and prompting only when several exist."""
    if args.channel:
        return args.channel
    configs = list(_can_module().detect_available_configs(interfaces=[args.interface]))
    configs = [c for c in configs if c.get("channel")]
    if not configs:
        sys.exit(f"[ERROR] No {args.interface} adapter detected. Connect one or pass --channel.")
    if len(configs) == 1:
        channel = configs[0]["channel"]
        print(f"[*] Using CAN adapter {channel}")
        return channel
    print("Multiple CAN adapters detected:")
    for index, cfg in enumerate(configs, 1):
        name = cfg.get("device_name", "")
        print(f"  {index}) {cfg['channel']}" + (f"  ({name})" if name else ""))
    return configs[_choose(len(configs), "Select adapter")]["channel"]


def _select_device(devices, args):
    """Return (node, uid, item), auto-selecting or prompting among discovered boards."""
    if not devices:
        sys.exit("[ERROR] No compatible devices responded on the bus.")
    if args.node is not None:
        item = devices.get(args.node)
        if not item:
            sys.exit(f"[ERROR] Node 0x{args.node:04x} not found in discovery.")
        if args.uid:
            uid = bytes.fromhex(args.uid)
            if len(uid) != 8 or item["uid"] != uid:
                sys.exit("[ERROR] Requested UID did not match discovery; nothing erased.")
        return args.node, item["uid"], item
    nodes = list(devices.items())
    if len(nodes) == 1:
        address, item = nodes[0]
        print(f"[*] Using node 0x{address:04x} uid={item['uid'].hex()}")
        return address, item["uid"], item
    print("Multiple devices detected:")
    for index, (address, item) in enumerate(nodes, 1):
        print(f"  {index}) node=0x{address:04x} uid={item['uid'].hex()} "
              f"mode={'bootloader' if item['mode'] else 'application'}")
    address, item = nodes[_choose(len(nodes), "Select board")]
    return address, item["uid"], item


def do_adapters(args):
    for adapter in _can_module().detect_available_configs(interfaces=[args.interface]):
        print(adapter)


def do_discover(args):
    global CFG, COMMANDS
    CFG, COMMANDS = _can_config(), _can_commands()
    args.channel = _select_channel(args)
    with _open_bus(args) as bus:
        devices = _discover_any(bus)
        if not devices:
            print("No compatible devices responded")
        _print_devices(devices)


def do_can(args):
    global CFG, COMMANDS
    CFG, COMMANDS = _can_config(), _can_commands()
    image = args.application or HEX_FILE
    if not os.path.isfile(image):
        print(f"[*] {os.path.basename(image)} not found, building first.")
        do_build()
    data = load_application(image)
    args.channel = _select_channel(args)
    with _open_bus(args) as bus:
        devices = _discover_any(bus)
        _print_devices(devices)
        node, uid, item = _select_device(devices, args)
        if item["version"] != CFG["PROTOCOL_VERSION"]:
            sys.exit("[ERROR] Device protocol did not match this firmware; nothing erased.")
        print(f"\n[*] Uploading {os.path.basename(image)} ({len(data)} bytes) over CAN "
              f"to node 0x{node:04x}...")
        if item["mode"] != 1:
            _trigger_enter(bus, node, uid, CFG)
            node, _ = _wait_mode(bus, uid, 1, cfg=_BOOT_CFG, extended=True)
        device = _Device(bus, node, uid)
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
                pass  # Reset may complete despite a lost final acknowledgement.
            node, _ = _wait_mode(bus, uid, 0, cfg=CFG, extended=False,
                                 attempts=20, settle=1.0)
            print("Application responded after reboot")
    print("\n[+] CAN UPLOAD SUCCESSFUL")


def _can_args(argv):
    parser = argparse.ArgumentParser(prog="build.py", add_help=False)
    parser.add_argument("command")
    parser.add_argument("--interface", default="pcan")
    parser.add_argument("--channel", help="Adapter channel; auto-detected when omitted")
    parser.add_argument("--bitrate", type=int, default=500_000)
    parser.add_argument("--node", type=lambda value: int(value, 0),
                        help="Target node; auto-selected from discovery when omitted")
    parser.add_argument("--uid", help="Optional full sixteen-hex-digit identity to pin the target")
    parser.add_argument("--application", help="Override image path (.hex or .bin)")
    parser.add_argument("--stay", action="store_true", help="Remain in recovery after upload")
    return parser.parse_args(argv)


def main():
    action = sys.argv[1].lower() if len(sys.argv) > 1 else "build"
    if action in ("adapters", "discover", "can"):
        args = _can_args(sys.argv[1:])
        {"adapters": do_adapters, "discover": do_discover, "can": do_can}[action](args)
        return
    if action == "clean":
        do_clean()
    elif action == "configure":
        do_configure()
    elif action == "rebuild":
        do_clean()
        do_build()
    elif action == "upload":
        do_upload()
    elif action == "swd":
        do_swd()
    elif action == "size":
        do_size()
    elif action == "build":
        do_build()
    else:
        print(__doc__)
        sys.exit(f"[ERROR] Unknown action: {action}")


if __name__ == "__main__":
    main()