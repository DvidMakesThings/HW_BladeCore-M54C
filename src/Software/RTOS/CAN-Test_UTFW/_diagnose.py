"""Diagnose CAN communication issue between two PCAN adapters.

Uses python-can directly (not UTFW) for a quick focused test.
Checks each adapter independently and then cross-adapter.
"""
import can
import time
import sys


def try_open(channel, bitrate=500000):
    """Try to open a PCAN channel, return bus or None."""
    try:
        bus = can.Bus(channel=channel, interface="pcan", bitrate=bitrate)
        print("  [OK] %s opened at %d bps" % (channel, bitrate))
        return bus
    except Exception as e:
        print("  [FAIL] %s: %s" % (channel, e))
        return None


def send_and_check(tx_bus, rx_bus, tx_name, rx_name, arb_id, data, timeout=2.0):
    """Send on tx_bus, try to receive on rx_bus."""
    # Flush any stale RX messages
    while rx_bus.recv(timeout=0.01) is not None:
        pass

    msg = can.Message(arbitration_id=arb_id, data=data, is_extended_id=False)
    try:
        tx_bus.send(msg, timeout=1.0)
        print("  TX on %s: ID=0x%03X data=%s" % (
            tx_name, arb_id,
            " ".join("%02X" % b for b in data)))
    except Exception as e:
        print("  TX FAILED on %s: %s" % (tx_name, e))
        return False

    rx_msg = rx_bus.recv(timeout=timeout)
    if rx_msg is None:
        print("  RX on %s: TIMEOUT (%.1fs)" % (rx_name, timeout))
        return False
    else:
        print("  RX on %s: ID=0x%03X data=%s" % (
            rx_name, rx_msg.arbitration_id,
            " ".join("%02X" % b for b in rx_msg.data)))
        if list(rx_msg.data) == data and rx_msg.arbitration_id == arb_id:
            print("  --> MATCH")
            return True
        else:
            print("  --> MISMATCH!")
            return False


def main():
    print("=" * 60)
    print("CAN Diagnostic Test")
    print("=" * 60)

    # Step 1: Open both channels
    print("\n[1] Opening channels at 500 kbit/s...")
    bus1 = try_open("PCAN_USBBUS1")
    bus2 = try_open("PCAN_USBBUS2")

    if not bus1 or not bus2:
        print("\nFailed to open both channels. Aborting.")
        if bus1:
            bus1.shutdown()
        if bus2:
            bus2.shutdown()
        return 1

    time.sleep(0.5)

    # Step 2: Check bus state
    print("\n[2] Checking bus state...")
    try:
        state1 = bus1.state
        state2 = bus2.state
        print("  USBBUS1 state: %s" % state1)
        print("  USBBUS2 state: %s" % state2)
    except Exception as e:
        print("  Could not read state: %s" % e)

    # Step 3: Send from USBBUS1 -> receive on USBBUS2
    print("\n[3] USBBUS1 -> USBBUS2...")
    ok_1to2 = send_and_check(bus1, bus2, "USBBUS1", "USBBUS2",
                             0x111, [0x11, 0x22, 0x33, 0x44])

    # Step 4: Send from USBBUS2 -> receive on USBBUS1
    print("\n[4] USBBUS2 -> USBBUS1...")
    ok_2to1 = send_and_check(bus2, bus1, "USBBUS2", "USBBUS1",
                             0x222, [0xAA, 0xBB, 0xCC, 0xDD])

    # Step 5: Try self-reception on each adapter
    print("\n[5] Self-reception test (each adapter receives its own TX)...")
    for name, bus in [("USBBUS1", bus1), ("USBBUS2", bus2)]:
        while bus.recv(timeout=0.01) is not None:
            pass
        msg = can.Message(arbitration_id=0x7FF, data=[0xFF], is_extended_id=False)
        try:
            bus.send(msg, timeout=1.0)
            rx = bus.recv(timeout=1.0)
            if rx:
                print("  %s self-RX: ID=0x%03X (likely loopback)" % (name, rx.arbitration_id))
            else:
                print("  %s self-RX: none" % name)
        except Exception as e:
            print("  %s send error: %s" % (name, e))

    # Cleanup
    bus1.shutdown()
    bus2.shutdown()

    print("\n" + "=" * 60)
    if ok_1to2 and ok_2to1:
        print("RESULT: PASS - Both directions work")
        return 0
    elif ok_1to2:
        print("RESULT: PARTIAL - Only USBBUS1->USBBUS2 works")
        print("  USBBUS2 can receive but not transmit (or USBBUS1 can't receive)")
        return 1
    elif ok_2to1:
        print("RESULT: PARTIAL - Only USBBUS2->USBBUS1 works")
        print("  USBBUS1 can receive but not transmit (or USBBUS2 can't receive)")
        return 1
    else:
        print("RESULT: FAIL - No communication in either direction")
        print("  Check: CAN bus wiring, termination, bitrate match")
        return 1


if __name__ == "__main__":
    sys.exit(main())
