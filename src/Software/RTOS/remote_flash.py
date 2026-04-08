"""Remote flash script for BladeCore-M54C PCAN adapter.

Sends a PCAN command (func=0xFF, num=0xFF, args=[B0 07 10 AD]) that
triggers the device to reboot into UF2 BOOTSEL mode.  Then uses
picotool to flash the new firmware.

Two trigger methods:
  1. USB: Send a special PCAN bulk command on EP1 OUT (requires the
     PEAK driver to be loaded and the device to be initialized).
  2. CAN: Send a magic CAN frame on the bus (requires another adapter
     on the same CAN bus).

Usage:
    python remote_flash.py [--method usb|can] [--uf2 path/to/firmware.uf2]
"""
import argparse
import ctypes
import os
import subprocess
import sys
import time


def find_picotool():
    """Locate picotool executable."""
    home = os.environ.get("USERPROFILE", os.path.expanduser("~"))
    candidates = [
        os.path.join(home, ".pico-sdk", "picotool", "2.2.0-a4",
                     "picotool", "picotool.exe"),
        "picotool",
    ]
    for c in candidates:
        if os.path.isfile(c):
            return c
    return "picotool"


def trigger_via_can(channel="PCAN_USBBUS1", bitrate=500000):
    """Send CAN bootloader trigger frame via another PCAN adapter."""
    try:
        import can
    except ImportError:
        print("ERROR: python-can not installed. pip install python-can")
        return False

    print("Opening %s at %d bps..." % (channel, bitrate))
    try:
        bus = can.Bus(channel=channel, interface="pcan", bitrate=bitrate)
    except Exception as e:
        print("ERROR: %s" % e)
        return False

    # BOOTSEL trigger frame: ID 0x7F0, DLC 8, [B0 07 5E 1E B0 07 5E 1E]
    # (CAN bootloader trigger would be [B0 07 10 AD ...] -- different!)
    magic = [0xB0, 0x07, 0x5E, 0x1E, 0xB0, 0x07, 0x5E, 0x1E]
    msg = can.Message(arbitration_id=0x7F0, data=magic, is_extended_id=False)

    print("Sending BOOTSEL trigger frame...")
    try:
        bus.send(msg, timeout=2.0)
        print("Trigger sent. Device should reboot into UF2 BOOTSEL mode.")
    except Exception as e:
        print("ERROR sending: %s" % e)
        bus.shutdown()
        return False

    bus.shutdown()
    return True


def trigger_via_usb():
    """Send BOOTSEL reboot command via PCAN USB bulk EP."""
    try:
        pcan = ctypes.WinDLL("PCANBasic")
    except OSError:
        print("ERROR: PCANBasic.dll not found")
        return False

    # Find our BladeCore device -- try each USB channel
    target_ch = None
    for ch_idx in range(1, 17):
        ch = 0x50 + ch_idx
        ret = pcan.CAN_Initialize(
            ctypes.c_ushort(ch), ctypes.c_ushort(0x001C),
            ctypes.c_ubyte(0), ctypes.c_uint(0), ctypes.c_ushort(0))
        if ret == 0:
            target_ch = ch
            # Use the first available channel -- the BOOTSEL command
            # goes through the PEAK driver's internal command pipe
            break

    if target_ch is None:
        print("ERROR: No PCAN USB channel found")
        return False

    # The PEAK driver doesn't expose raw EP1 access, but we can use
    # CAN_SetValue with PCAN_DEVICE_ID to trigger a bulk command.
    # However, func=0xFF is not a standard PCAN parameter.
    #
    # Instead, we uninit and re-init. The BOOTSEL trigger requires
    # raw bulk access which PCANBasic doesn't provide.
    #
    # Fallback: use the CAN trigger method if possible.
    pcan.CAN_Uninitialize(ctypes.c_ushort(target_ch))
    print("NOTE: USB direct trigger requires raw USB access.")
    print("      The PEAK driver does not expose raw bulk endpoints.")
    print("      Use --method can instead, or hold BOOTSEL manually.")
    return False


def trigger_bootsel_rom():
    """Try picotool reboot command (works if device has RP2350 ROM)."""
    picotool = find_picotool()
    print("Trying picotool reboot -f -u ...")
    result = subprocess.run(
        [picotool, "reboot", "-f", "-u"],
        capture_output=True, text=True, timeout=10
    )
    if result.returncode == 0:
        print("Device rebooted into BOOTSEL mode.")
        return True
    print("picotool reboot failed (device not visible as RP2xxx).")
    return False


def flash_firmware(uf2_path):
    """Flash UF2 firmware via picotool."""
    picotool = find_picotool()
    print("Flashing %s ..." % uf2_path)
    result = subprocess.run(
        [picotool, "load", uf2_path, "-fx"],
        capture_output=True, text=True, timeout=60
    )
    print(result.stdout)
    if result.stderr:
        print(result.stderr)
    return result.returncode == 0


def main():
    parser = argparse.ArgumentParser(
        description="Remote flash BladeCore-M54C PCAN adapter")
    parser.add_argument("--method", choices=["usb", "can", "auto"],
                        default="auto",
                        help="Trigger method (default: auto)")
    parser.add_argument("--uf2", default="build/BladeCore_CAN.uf2",
                        help="UF2 firmware path")
    parser.add_argument("--channel", default="PCAN_USBBUS1",
                        help="PCAN channel for CAN trigger (default: USBBUS1)")
    parser.add_argument("--bitrate", type=int, default=500000,
                        help="CAN bitrate (default: 500000)")
    parser.add_argument("--trigger-only", action="store_true",
                        help="Only trigger reboot, don't flash")
    args = parser.parse_args()

    if not args.trigger_only and not os.path.isfile(args.uf2):
        print("ERROR: UF2 file not found: %s" % args.uf2)
        return 1

    # Trigger reboot into BOOTSEL
    triggered = False

    if args.method in ("auto", "can"):
        triggered = trigger_via_can(args.channel, args.bitrate)

    if not triggered and args.method in ("auto", "usb"):
        triggered = trigger_via_usb()

    if not triggered and args.method == "auto":
        triggered = trigger_bootsel_rom()

    if not triggered:
        print("\nCould not trigger BOOTSEL remotely.")
        print("Please hold BOOTSEL button while plugging USB.")
        input("Press Enter when device is in BOOTSEL mode...")

    if args.trigger_only:
        print("Trigger sent. Exiting.")
        return 0

    # Wait for device to enumerate in BOOTSEL mode
    print("Waiting for BOOTSEL enumeration...")
    time.sleep(3)

    # Flash
    if flash_firmware(args.uf2):
        print("\nFlash complete. Device is running new firmware.")
        return 0
    else:
        print("\nFlash failed. Is the device in BOOTSEL mode?")
        return 1


if __name__ == "__main__":
    sys.exit(main())
