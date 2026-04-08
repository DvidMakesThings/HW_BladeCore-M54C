#!/usr/bin/env python3
"""BladeCore-M54C CAN adapter test.

Test Purpose:
    Verify the BladeCore-M54C board, running PCAN-USB adapter firmware,
    can reliably bridge CAN 2.0 frames between a host PC and the CAN bus.

Test Description:
    Two PCAN adapters are connected to the same CAN bus:
      - DUT: BladeCore-M54C with PCAN-USB clone firmware (PID 0x000C)
      - REF: store-bought PCAN-USB FD adapter (PID 0x0012)

    Each test sends a frame from one adapter and verifies it arrives
    intact on the other.  The ``loopback`` helper in UTFW opens both
    channels, transmits on TX, and checks the received data on RX.

Test Steps:
   1.   DUT -> REF basic loopback [DE AD BE EF]
   2.   REF -> DUT basic loopback [CA FE BA BE]
   3.x  DLC sweep 0..8 (DUT -> REF)
   4.   All-zeros  [00]*8   (DUT -> REF)
   5.   All-ones   [FF]*8   (DUT -> REF)
   6.   Alternating [AA 55]*4 (DUT -> REF)
   7.x  Walking bit (DUT -> REF, 8 positions)
   8.   ID low  0x001 (DUT -> REF)
   9.   ID high 0x7FF (DUT -> REF)
  10.x  ID sweep: 0x010, 0x080, 0x100, 0x200, 0x3FF, 0x555, 0x7FE
  11.x  Burst 10 frames DUT -> REF
  12.x  Burst 10 frames REF -> DUT
  13.x  Burst 20 DLC=8 frames REF -> DUT
  14.x  DLC sweep 0..8 (REF -> DUT)
  15.   All-zeros  [00]*8   (REF -> DUT)
  16.   All-ones   [FF]*8   (REF -> DUT)
  17.   Alternating [AA 55]*4 (REF -> DUT)
  18.x  Walking bit (REF -> DUT, 8 positions)
  19.x  Byte boundary: 0x00/0x80/0xFF per position (REF -> DUT)
  20.x  Counter payload 10 frames (REF -> DUT)

Expected Results:
    All steps PASS.  Every received frame matches the transmitted
    payload byte-for-byte and has the correct arbitration ID.

Hardware Setup:
    BladeCore-M54C          PCAN-USB FD (store-bought)
    CAN_P (M.2 pin 28) --  CAN-H
    CAN_N (M.2 pin 30) --  CAN-L
    GND                 --  GND
    120 ohm termination on each end.
"""

import sys

from UTFW.core import run_test_with_teardown, STE
from UTFW.core.utilities import get_hwconfig
from UTFW.modules.ext_tools import PU2CANFD


def _lb(hw, name, tx, rx, arb_id, data, timeout=5.0):
    """Shorthand for a loopback step."""
    return PU2CANFD.can.loopback(
        name=name,
        tx_channel=tx,
        rx_channel=rx,
        arb_id=arb_id,
        data=data,
        bitrate=hw.CAN_BITRATE,
        bustype=hw.CAN_BUSTYPE,
        timeout=timeout,
    )


class tc_bladecore_m54c_can:
    """Two-adapter CAN loopback test for BladeCore-M54C PCAN adapter."""

    def __init__(self):
        pass

    def setup(self):
        hw = get_hwconfig()
        dut = hw.DUT_CHANNEL
        ref = hw.REF_CHANNEL

        return [
            # ============================================================
            # Basic bidirectional
            # ============================================================

            # 1. DUT -> REF basic loopback
            _lb(hw, "DUT->REF [DE AD BE EF]",
                dut, ref, 0x100, [0xDE, 0xAD, 0xBE, 0xEF]),

            # 2. REF -> DUT basic loopback
            _lb(hw, "REF->DUT [CA FE BA BE]",
                ref, dut, 0x200, [0xCA, 0xFE, 0xBA, 0xBE]),

            # ============================================================
            # DLC sweep (DUT -> REF)
            # ============================================================

            # 3. DLC 0..8
            STE(
                *[_lb(hw, "DLC=%d DUT->REF" % dlc,
                      dut, ref, 0x300, list(range(dlc)))
                  for dlc in range(9)],
                name="DLC sweep 0..8 (DUT->REF)",
            ),

            # ============================================================
            # Data pattern tests (DUT -> REF)
            # ============================================================

            # 4. All zeros
            _lb(hw, "All-zeros [00]*8 DUT->REF",
                dut, ref, 0x400, [0x00] * 8),

            # 5. All ones
            _lb(hw, "All-ones [FF]*8 DUT->REF",
                dut, ref, 0x400, [0xFF] * 8),

            # 6. Alternating bits
            _lb(hw, "Alternating [AA 55]*4 DUT->REF",
                dut, ref, 0x400, [0xAA, 0x55] * 4),

            # 7. Walking bit
            STE(
                *[_lb(hw, "Walking bit %d DUT->REF" % b,
                      dut, ref, 0x400,
                      [(1 << b) if i == b else 0x00 for i in range(8)])
                  for b in range(8)],
                name="Walking bit (DUT->REF)",
            ),

            # ============================================================
            # Arbitration ID range (DUT -> REF)
            # ============================================================

            # 8. ID low
            _lb(hw, "ID 0x001 DUT->REF",
                dut, ref, 0x001, [0x11]),

            # 9. ID high
            _lb(hw, "ID 0x7FF DUT->REF",
                dut, ref, 0x7FF, [0x77]),

            # 10. ID sweep
            STE(
                *[_lb(hw, "ID 0x%03X DUT->REF" % aid,
                      dut, ref, aid, [aid & 0xFF, (aid >> 8) & 0xFF])
                  for aid in [0x010, 0x080, 0x100, 0x200,
                              0x3FF, 0x555, 0x7FE]],
                name="ID sweep (DUT->REF)",
            ),

            # ============================================================
            # Burst tests (unidirectional)
            # ============================================================

            # 11. Burst DUT -> REF (10 frames)
            STE(
                *[_lb(hw, "Burst %d DUT->REF" % i,
                      dut, ref, 0x500 + i,
                      [0xB0 + i, i, i, i])
                  for i in range(10)],
                name="Burst DUT->REF (10 frames)",
            ),

            # 12. Burst REF -> DUT (10 frames)
            STE(
                *[_lb(hw, "Burst %d REF->DUT" % i,
                      ref, dut, 0x600 + i,
                      [0xC0 + i, i, i, i])
                  for i in range(10)],
                name="Burst REF->DUT (10 frames)",
            ),

            # 13. Burst REF -> DUT (20 DLC=8 frames)
            STE(
                *[_lb(hw, "Burst DLC8 %02d REF->DUT" % i,
                      ref, dut, 0x100 + i,
                      [(i + j) & 0xFF for j in range(8)])
                  for i in range(20)],
                name="Burst REF->DUT (20 x DLC=8)",
            ),

            # ============================================================
            # DLC sweep (REF -> DUT)
            # ============================================================

            # 14. DLC 0..8
            STE(
                *[_lb(hw, "DLC=%d REF->DUT" % dlc,
                      ref, dut, 0x300, list(range(dlc)))
                  for dlc in range(9)],
                name="DLC sweep 0..8 (REF->DUT)",
            ),

            # ============================================================
            # Data pattern tests (REF -> DUT)
            # ============================================================

            # 15. All zeros
            _lb(hw, "All-zeros [00]*8 REF->DUT",
                ref, dut, 0x400, [0x00] * 8),

            # 16. All ones
            _lb(hw, "All-ones [FF]*8 REF->DUT",
                ref, dut, 0x400, [0xFF] * 8),

            # 17. Alternating bits
            _lb(hw, "Alternating [AA 55]*4 REF->DUT",
                ref, dut, 0x400, [0xAA, 0x55] * 4),

            # 18. Walking bit (REF -> DUT)
            STE(
                *[_lb(hw, "Walking bit %d REF->DUT" % b,
                      ref, dut, 0x400,
                      [(1 << b) if i == b else 0x00 for i in range(8)])
                  for b in range(8)],
                name="Walking bit (REF->DUT)",
            ),

            # ============================================================
            # Byte boundary values (REF -> DUT)
            # ============================================================

            # 19. Each byte position at 0x00, 0x80, 0xFF
            STE(
                *[_lb(hw, "Byte[%d]=0x%02X REF->DUT" % (pos, val),
                      ref, dut, 0x400,
                      [val if i == pos else 0x55 for i in range(8)])
                  for pos in range(8)
                  for val in [0x00, 0x80, 0xFF]],
                name="Byte boundary values (REF->DUT)",
            ),

            # ============================================================
            # Counter payload (REF -> DUT)
            # ============================================================

            # 20. Sequential counter frames
            STE(
                *[_lb(hw, "Counter %02d REF->DUT" % i,
                      ref, dut, 0x700,
                      [i, (~i) & 0xFF, i * 2, (i * 2 + 1) & 0xFF,
                       0xDE, 0xAD, (i >> 4) & 0xFF, i & 0x0F])
                  for i in range(10)],
                name="Counter payload (10 frames REF->DUT)",
            ),
        ]


def main():
    test_instance = tc_bladecore_m54c_can()
    return run_test_with_teardown(
        test_class_instance=test_instance,
        test_name="tc_bladecore_m54c_can",
        reports_dir="report_tc_bladecore_m54c_can",
    )


if __name__ == "__main__":
    sys.exit(main())
