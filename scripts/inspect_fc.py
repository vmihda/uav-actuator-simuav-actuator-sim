#!/usr/bin/env python3
"""Read ArduPilot identity and UART6/passthrough parameters without changing them."""
import argparse
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / ".pio" / "python-deps"))
from pymavlink import mavutil


def read_parameter(connection, name, timeout=4):
    connection.mav.param_request_read_send(connection.target_system,
                                           connection.target_component, name.encode(), -1)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        message = connection.recv_match(type="PARAM_VALUE", blocking=True, timeout=0.5)
        if message and message.param_id == name:
            return message.param_value, message.param_type
    raise TimeoutError("No parameter response: " + name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/cu.usbmodem2101")
    args = parser.parse_args()
    connection = mavutil.mavlink_connection(args.port, baud=115200, source_system=255)
    try:
        heartbeat = connection.wait_heartbeat(timeout=10)
        if not heartbeat:
            raise TimeoutError("No ArduPilot heartbeat")
        print(f"System {connection.target_system}, component {connection.target_component}, "
              f"armed={bool(heartbeat.base_mode & mavutil.mavlink.MAV_MODE_FLAG_SAFETY_ARMED)}")
        connection.mav.command_long_send(connection.target_system, connection.target_component,
                                        mavutil.mavlink.MAV_CMD_REQUEST_MESSAGE, 0,
                                        mavutil.mavlink.MAVLINK_MSG_ID_AUTOPILOT_VERSION, 0, 0, 0, 0, 0, 0)
        version = connection.recv_match(type="AUTOPILOT_VERSION", blocking=True, timeout=3)
        if version:
            value = version.flight_sw_version
            print(f"Firmware {value >> 24}.{(value >> 16) & 255}.{(value >> 8) & 255}, type={value & 255}")
        for name in ["SERIAL6_PROTOCOL", "SERIAL6_BAUD", "SERIAL6_OPTIONS",
                     "SERIAL_PASS1", "SERIAL_PASS2", "SERIAL_PASSTIMO"]:
            value, kind = read_parameter(connection, name)
            print(f"{name}={value:g} (type {kind})")
    finally:
        connection.close()


if __name__ == "__main__":
    main()
