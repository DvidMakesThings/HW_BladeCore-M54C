"""hardware_config.py - BladeCore-M54C CAN adapter test configuration.

Two-adapter topology:
    BladeCore-M54C  (PCAN-USB clone, VID 0C72 PID 000C)
    Store-bought    (PCAN-USB FD,    VID 0C72 PID 0012)

Both adapters are on the same CAN bus. Tests send from one and
receive on the other to verify the BladeCore CAN bridge works.

Channel assignment:
    The PEAK driver enumerates adapters in order of USB arrival.
    If the BladeCore is plugged in *before* the store-bought adapter
    it will typically appear as PCAN_USBBUS1. Adjust if needed.
"""

CAN_BUSTYPE = "pcan"
CAN_BITRATE = 1000000  # 1 Mbit/s (both adapters must match)

# -- Channel mapping (adjust if Windows enumeration order differs) --
DUT_CHANNEL = "PCAN_USBBUS2"       # BladeCore-M54C
REF_CHANNEL = "PCAN_USBBUS1"       # Store-bought PCAN-USB FD
