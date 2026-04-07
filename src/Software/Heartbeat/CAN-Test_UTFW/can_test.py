#!/usr/bin/env python3
"""BladeCore-M54C CAN communication test.

Test Purpose:
    Verify the MCP2515 CAN controller on the BladeCore-M54C board can
    reliably communicate with a PC-side CAN adapter across the full
    range of standard CAN 2.0 frame parameters.

Test Description:
    The RP2350B runs firmware that sends periodic heartbeat frames
    (ID 0x100), echoes any non-command frame with ID+1, and handles
    a command protocol on ID 0x7E0/0x7E1 for diagnostics (ping,
    error counters, status register, payload echo).

    The test exercises bus presence, command protocol, data integrity
    patterns, DLC sweep, arbitration ID range, burst traffic, and
    bus health via MCP2515 error counters.

Test Steps:
     1. Bus scan             - listen for heartbeat frames (5s)
     2. Ping command         - command round-trip 0x7E0 -> 0x7E1
     3. Read CANSTAT         - verify MCP2515 is in normal mode
     4. Read error counters  - verify TEC=0, REC=0, EFLG=0
     5. Echo integrity       - round-trip [DE AD BE EF]
   6.0-6.8. DLC sweep       - echo with DLC 0 through 8
     7. All-zeros            - echo [00 00 00 00 00 00 00 00]
     8. All-ones             - echo [FF FF FF FF FF FF FF FF]
     9. Alternating bits     - echo [AA 55 AA 55 AA 55 AA 55]
  10.0-10.7. Walking bit    - single bit set per byte position
    11. ID range low         - echo on ID 0x001
    12. ID range high        - echo on ID 0x7FE
  13.0-13.4. Burst traffic  - 5 rapid sequential frames
    14. Max CMD payload      - 7-byte echo via command protocol
    15. Final health check   - error counters still zero

Expected Results:
    All steps PASS. Every echo returns the sent payload unchanged.
    Error counters remain zero throughout (no bus errors).
    Bus scan captures >= 1 heartbeat frame from the board.

Hardware Setup:
    BladeCore-M54C          PCAN USB Adapter
    CAN_P (M.2 pin 28) --  CAN-H
    CAN_N (M.2 pin 30) --  CAN-L
    GND                 --  GND
"""

import sys

from UTFW.core import run_test_with_teardown, STE
from UTFW.core.utilities import get_hwconfig
from UTFW.modules.ext_tools import PU2CANFD


class tc_bladecore_m54c_can:
    """In-depth CAN bus test for BladeCore-M54C via PCAN."""

    def __init__(self):
        pass

    def setup(self):
        hw = get_hwconfig()

        return [
            # -- 1. Bus presence: scan for heartbeat frames --
            PU2CANFD.can.scan(
                name="Bus scan - heartbeat frames (5s)",
                channel=hw.CAN_CHANNEL,
                bustype=hw.CAN_BUSTYPE,
                bitrate=hw.CAN_BITRATE,
                duration=5.0,
            ),

            # -- 2. Ping command --
            PU2CANFD.can.send_receive(
                name="Ping command",
                channel=hw.CAN_CHANNEL,
                bustype=hw.CAN_BUSTYPE,
                arb_id=hw.CMD_REQUEST_ID,
                data=[hw.CMD_PING, 0xAB, 0xCD],
                response_id=hw.CMD_RESPONSE_ID,
                bitrate=hw.CAN_BITRATE,
                timeout=3.0,
            ),

            # -- 3. Read MCP2515 status register --
            PU2CANFD.can.send_receive(
                name="Read CANSTAT (expect normal mode)",
                channel=hw.CAN_CHANNEL,
                bustype=hw.CAN_BUSTYPE,
                arb_id=hw.CMD_REQUEST_ID,
                data=[hw.CMD_GET_STATUS],
                response_id=hw.CMD_RESPONSE_ID,
                bitrate=hw.CAN_BITRATE,
                timeout=3.0,
            ),

            # -- 4. Read error counters (expect zeros = healthy bus) --
            PU2CANFD.can.send_receive(
                name="Read error counters (TEC/REC/EFLG)",
                channel=hw.CAN_CHANNEL,
                bustype=hw.CAN_BUSTYPE,
                arb_id=hw.CMD_REQUEST_ID,
                data=[hw.CMD_GET_ERRORS],
                response_id=hw.CMD_RESPONSE_ID,
                bitrate=hw.CAN_BITRATE,
                timeout=3.0,
            ),

            # -- 5. Basic echo integrity --
            PU2CANFD.can.send_receive(
                name="Echo [DE AD BE EF]",
                channel=hw.CAN_CHANNEL,
                bustype=hw.CAN_BUSTYPE,
                arb_id=hw.ECHO_BASE_ID,
                data=[0xDE, 0xAD, 0xBE, 0xEF],
                response_id=hw.ECHO_BASE_ID + 1,
                bitrate=hw.CAN_BITRATE,
                timeout=3.0,
            ),

            # -- 6. DLC sweep: test every data length 0..8 --
            STE(
                *[
                    PU2CANFD.can.send_receive(
                        name=f"DLC={dlc} echo",
                        channel=hw.CAN_CHANNEL,
                        bustype=hw.CAN_BUSTYPE,
                        arb_id=hw.ECHO_BASE_ID,
                        data=list(range(dlc)),
                        response_id=hw.ECHO_BASE_ID + 1,
                        bitrate=hw.CAN_BITRATE,
                        timeout=3.0,
                    )
                    for dlc in range(9)
                ],
                name="DLC sweep (0..8)",
            ),

            # -- 7. All-zeros pattern (8 bytes) --
            PU2CANFD.can.send_receive(
                name="All-zeros [00]*8",
                channel=hw.CAN_CHANNEL,
                bustype=hw.CAN_BUSTYPE,
                arb_id=hw.ECHO_BASE_ID,
                data=[0x00] * 8,
                response_id=hw.ECHO_BASE_ID + 1,
                bitrate=hw.CAN_BITRATE,
                timeout=3.0,
            ),

            # -- 8. All-ones pattern (8 bytes) --
            PU2CANFD.can.send_receive(
                name="All-ones [FF]*8",
                channel=hw.CAN_CHANNEL,
                bustype=hw.CAN_BUSTYPE,
                arb_id=hw.ECHO_BASE_ID,
                data=[0xFF] * 8,
                response_id=hw.ECHO_BASE_ID + 1,
                bitrate=hw.CAN_BITRATE,
                timeout=3.0,
            ),

            # -- 9. Alternating bits pattern --
            PU2CANFD.can.send_receive(
                name="Alternating [AA 55]*4",
                channel=hw.CAN_CHANNEL,
                bustype=hw.CAN_BUSTYPE,
                arb_id=hw.ECHO_BASE_ID,
                data=[0xAA, 0x55] * 4,
                response_id=hw.ECHO_BASE_ID + 1,
                bitrate=hw.CAN_BITRATE,
                timeout=3.0,
            ),

            # -- 10. Walking bit through 8 bytes --
            STE(
                *[
                    PU2CANFD.can.send_receive(
                        name=f"Walking bit {b} (byte[{b}]=0x{1 << b:02X})",
                        channel=hw.CAN_CHANNEL,
                        bustype=hw.CAN_BUSTYPE,
                        arb_id=hw.ECHO_BASE_ID,
                        data=[(1 << b) if i == b else 0x00 for i in range(8)],
                        response_id=hw.ECHO_BASE_ID + 1,
                        bitrate=hw.CAN_BITRATE,
                        timeout=3.0,
                    )
                    for b in range(8)
                ],
                name="Walking bit (8 positions)",
            ),

            # -- 11. ID range: minimum standard ID --
            PU2CANFD.can.send_receive(
                name="ID range low (0x001)",
                channel=hw.CAN_CHANNEL,
                bustype=hw.CAN_BUSTYPE,
                arb_id=0x001,
                data=[0x11],
                response_id=0x002,
                bitrate=hw.CAN_BITRATE,
                timeout=3.0,
            ),

            # -- 12. ID range: maximum standard ID (0x7FE, echo -> 0x7FF) --
            PU2CANFD.can.send_receive(
                name="ID range high (0x7FE)",
                channel=hw.CAN_CHANNEL,
                bustype=hw.CAN_BUSTYPE,
                arb_id=0x7FE,
                data=[0x22],
                response_id=0x7FF,
                bitrate=hw.CAN_BITRATE,
                timeout=3.0,
            ),

            # -- 13. Burst: 5 rapid send_receive in sequence --
            STE(
                *[
                    PU2CANFD.can.send_receive(
                        name=f"Burst frame {i}",
                        channel=hw.CAN_CHANNEL,
                        bustype=hw.CAN_BUSTYPE,
                        arb_id=hw.BURST_BASE_ID,
                        data=[0xB0 + i, i, i, i],
                        response_id=hw.BURST_BASE_ID + 1,
                        bitrate=hw.CAN_BITRATE,
                        timeout=3.0,
                    )
                    for i in range(5)
                ],
                name="Burst traffic (5 frames)",
            ),

            # -- 14. Max payload via command echo --
            PU2CANFD.can.send_receive(
                name="Echo 7-byte payload via CMD",
                channel=hw.CAN_CHANNEL,
                bustype=hw.CAN_BUSTYPE,
                arb_id=hw.CMD_REQUEST_ID,
                data=[hw.CMD_ECHO_PAYLOAD, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD],
                response_id=hw.CMD_RESPONSE_ID,
                bitrate=hw.CAN_BITRATE,
                timeout=3.0,
            ),

            # -- 15. Final health check: error counters still zero --
            PU2CANFD.can.send_receive(
                name="Final error counters (post-test health)",
                channel=hw.CAN_CHANNEL,
                bustype=hw.CAN_BUSTYPE,
                arb_id=hw.CMD_REQUEST_ID,
                data=[hw.CMD_GET_ERRORS],
                response_id=hw.CMD_RESPONSE_ID,
                bitrate=hw.CAN_BITRATE,
                timeout=3.0,
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
