"""Trigger BOOTSEL reboot on the DUT and flash new firmware.

Opens the DUT channel (so MCP2515 enters normal mode), sends the
BOOTSEL magic frame from the REF adapter, waits for BOOTSEL, then
flashes via picotool.

Usage:
    python _bootsel_flash.py [--dut 2] [--ref 1]
"""
import ctypes
import os
import subprocess
import sys
import time

pcan = ctypes.WinDLL("PCANBasic")

DUT_IDX = int(sys.argv[sys.argv.index("--dut") + 1]) if "--dut" in sys.argv else 2
REF_IDX = int(sys.argv[sys.argv.index("--ref") + 1]) if "--ref" in sys.argv else 1
DUT_CH = 0x50 + DUT_IDX
REF_CH = 0x50 + REF_IDX
UF2 = "build/BladeCore_CAN.uf2"

PICOTOOL = os.path.join(
    os.environ.get("USERPROFILE", ""),
    ".pico-sdk", "picotool", "2.2.0-a4", "picotool", "picotool.exe")


class TPCANMsg(ctypes.Structure):
    _fields_ = [
        ("ID", ctypes.c_uint),
        ("MSGTYPE", ctypes.c_ubyte),
        ("LEN", ctypes.c_ubyte),
        ("DATA", ctypes.c_ubyte * 8),
    ]


# Step 1: Open DUT so MCP2515 goes to normal mode
print("Opening DUT (USBBUS%d) ..." % DUT_IDX)
r = pcan.CAN_Initialize(ctypes.c_ushort(DUT_CH), ctypes.c_ushort(0x001C),
                         ctypes.c_ubyte(0), ctypes.c_uint(0), ctypes.c_ushort(0))
if r != 0:
    print("DUT init failed: 0x%04X" % r)
    sys.exit(1)

# Step 2: Open REF
print("Opening REF (USBBUS%d) ..." % REF_IDX)
r = pcan.CAN_Initialize(ctypes.c_ushort(REF_CH), ctypes.c_ushort(0x001C),
                         ctypes.c_ubyte(0), ctypes.c_uint(0), ctypes.c_ushort(0))
if r != 0:
    print("REF init failed: 0x%04X" % r)
    pcan.CAN_Uninitialize(ctypes.c_ushort(DUT_CH))
    sys.exit(1)

# Give MCP2515 time to enter normal mode
time.sleep(0.2)

# Step 3: Send BOOTSEL trigger from REF
print("Sending BOOTSEL trigger (ID 0x7F0) from REF ...")
msg = TPCANMsg()
msg.ID = 0x7F0
msg.MSGTYPE = 0
msg.LEN = 8
magic = [0xB0, 0x07, 0x5E, 0x1E, 0xB0, 0x07, 0x5E, 0x1E]
for i in range(8):
    msg.DATA[i] = magic[i]

r = pcan.CAN_Write(ctypes.c_ushort(REF_CH), ctypes.byref(msg))
print("Write result: 0x%04X" % r)

# Step 4: Wait for device to disconnect and enter BOOTSEL
time.sleep(0.5)
pcan.CAN_Uninitialize(ctypes.c_ushort(DUT_CH))
pcan.CAN_Uninitialize(ctypes.c_ushort(REF_CH))

print("Waiting for BOOTSEL enumeration ...")
time.sleep(3)

# Step 5: Flash
print("Flashing %s ..." % UF2)
result = subprocess.run(
    [PICOTOOL, "load", UF2, "-fx"],
    capture_output=True, text=True, timeout=60)
print(result.stdout)
if result.stderr:
    print(result.stderr)
if result.returncode == 0:
    print("Flash complete.")
else:
    print("Flash FAILED (returncode %d)" % result.returncode)
sys.exit(result.returncode)
