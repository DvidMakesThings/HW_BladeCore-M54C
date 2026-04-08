#!/usr/bin/env python3
"""Continuous CAN traffic generator for oscilloscope eye diagrams.

Sends alternating 0xAA/0x55 frames from REF adapter for 10 seconds,
giving the scope enough transitions to build a clean eye pattern.
The DUT receives but we don't check -- this is purely for analog
signal quality observation.

Usage:
    python can_eye.py
"""

import time
from ctypes import c_ubyte, c_uint, c_ushort, WinDLL

from hardware_config import CAN_BITRATE, REF_CHANNEL

# --- PCAN constants ---
PCAN_CHANNELS = {
    "PCAN_USBBUS1": 0x51,
    "PCAN_USBBUS2": 0x52,
}

PCAN_BITRATES = {
    125000:  0x031C,
    250000:  0x011C,
    500000:  0x001C,
    1000000: 0x0014,
}

DURATION_S = 100
ARB_ID = 0x555

# Two complementary patterns -- maximises bit transitions on the bus
PATTERN_A = bytes([0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55])
PATTERN_B = bytes([0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA])


class TPCANMsg(object):
    """Minimal ctypes-free PCAN message (use raw CAN_Write call)."""
    pass


def main():
    pcan = WinDLL("PCANBasic")

    ch = c_ushort(PCAN_CHANNELS[REF_CHANNEL])
    btr = c_ushort(PCAN_BITRATES[CAN_BITRATE])

    rc = pcan.CAN_Initialize(ch, btr, c_ubyte(0), c_uint(0), c_ushort(0))
    if rc != 0:
        print("CAN_Initialize failed: 0x%04X" % rc)
        return

    # Build a raw 16-byte PCAN message buffer:
    #   DWORD ID  (4 bytes)
    #   BYTE  type (0 = standard)
    #   BYTE  dlc
    #   BYTE  data[8]
    import ctypes

    class TPCANMsg(ctypes.Structure):
        _fields_ = [
            ("ID", ctypes.c_uint32),
            ("MSGTYPE", ctypes.c_ubyte),
            ("LEN", ctypes.c_ubyte),
            ("DATA", ctypes.c_ubyte * 8),
        ]

    msg = TPCANMsg()
    msg.ID = ARB_ID
    msg.MSGTYPE = 0x00  # standard
    msg.LEN = 8

    print("Sending CAN frames on %s at %d bps for %d s ..." %
          (REF_CHANNEL, CAN_BITRATE, DURATION_S))
    print("  ID=0x%03X  patterns=0xAA55/0x55AA  DLC=8" % ARB_ID)
    print("  Press Ctrl+C to stop early.\n")

    count = 0
    errors = 0
    toggle = False
    t_start = time.perf_counter()

    try:
        while True:
            elapsed = time.perf_counter() - t_start
            if elapsed >= DURATION_S:
                break

            pat = PATTERN_B if toggle else PATTERN_A
            for i in range(8):
                msg.DATA[i] = pat[i]
            toggle = not toggle

            rc = pcan.CAN_Write(ch, ctypes.byref(msg))
            if rc == 0:
                count += 1
            else:
                errors += 1
                # 0x0040 = TX queue full, back off briefly
                if rc == 0x0040:
                    time.sleep(0.0001)

    except KeyboardInterrupt:
        elapsed = time.perf_counter() - t_start
        print("\nStopped by user.")

    elapsed = time.perf_counter() - t_start
    pcan.CAN_Uninitialize(ch)

    rate = count / elapsed if elapsed > 0 else 0
    print("Done. %d frames in %.2f s  (%.0f frames/s, %d errors)" %
          (count, elapsed, rate, errors))


if __name__ == "__main__":
    main()
