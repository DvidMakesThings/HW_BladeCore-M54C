"""hardware_config.py - BladeCore-M54C CAN test hardware configuration."""

# -- CAN bus (PCAN USB adapter) --
CAN_CHANNEL = "PCAN_USBBUS1"
CAN_BUSTYPE = "pcan"
CAN_BITRATE = 1000000  # 1 Mbps

# -- MCP2515 command protocol --
CMD_REQUEST_ID = 0x7E0
CMD_RESPONSE_ID = 0x7E1

CMD_PING = 0x01
CMD_GET_ERRORS = 0x02
CMD_GET_STATUS = 0x05
CMD_ECHO_PAYLOAD = 0x06

# -- Echo convention --
ECHO_BASE_ID = 0x200
BURST_BASE_ID = 0x300
