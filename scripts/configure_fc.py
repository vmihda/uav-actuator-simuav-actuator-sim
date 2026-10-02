#!/usr/bin/env python3
"""Reserve SpeedyBee F405 V3 UART6 for the custom ESP32 simulator protocol."""
import argparse
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / ".pio" / "python-deps"))
from pymavlink import mavutil
from inspect_fc import read_parameter


def set_parameter(connection, name, value, kind):
    for _ in range(3):
        connection.mav.param_set_send(connection.target_system, connection.target_component,
                                     name.encode(), float(value), kind)
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            message = connection.recv_match(type="PARAM_VALUE", blocking=True, timeout=0.25)
            if message and message.param_id == name:
                if abs(message.param_value - value) < 0.001:
                    print(f"Verified {name}={value:g}", flush=True)
                    return
                break
    raise RuntimeError("Parameter write was not verified: " + name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/cu.usbmodem2101")
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--reboot", action="store_true")
    args = parser.parse_args()
    connection = mavutil.mavlink_connection(args.port, baud=115200, source_system=255)
    try:
        heartbeat = connection.wait_heartbeat(timeout=10)
        if not heartbeat:
            raise TimeoutError("No ArduPilot heartbeat")
        if heartbeat.base_mode & mavutil.mavlink.MAV_MODE_FLAG_SAFETY_ARMED:
            raise RuntimeError("Flight controller is armed; UART reconfiguration cancelled")
        # In ArduPilot 4.7.1, -1 physically disables RX/TX. Legacy console 0
        # initializes raw UART pins and creates neither a GPS nor MAVLink driver.
        desired = {"SERIAL6_PROTOCOL": 0, "SERIAL6_BAUD": 115, "SERIAL6_OPTIONS": 0,
                   "GPS1_TYPE": 0, "GPS2_TYPE": 0}
        current = {}
        for name, value in desired.items():
            previous, kind = read_parameter(connection, name)
            current[name] = {"value": previous, "type": kind}
            print(f"{name}: {previous:g} -> {value:g}", flush=True)
        if not args.apply:
            return
        # Kept outside .pio: build-artifact directories are routinely deleted.
        snapshot = ROOT / "backups" / "fc-parameters-before.json"
        if not snapshot.exists():
            if all(current[name]["value"] == value for name, value in desired.items()):
                # Values already applied are not the originals; never record them as such.
                print(f"No snapshot written: {snapshot} is missing and the flight controller "
                      "already has the simulator configuration", flush=True)
            else:
                snapshot.parent.mkdir(exist_ok=True)
                snapshot.write_text(json.dumps({"port": args.port, "parameters": current}, indent=2) + "\n")
        for name, value in desired.items():
            if current[name]["value"] != value:
                set_parameter(connection, name, value, current[name]["type"])
            else:
                print(f"Already set {name}={value:g}", flush=True)
        if args.reboot:
            print("Rebooting disarmed flight controller to apply port assignment", flush=True)
            connection.mav.command_long_send(connection.target_system, connection.target_component,
                                             mavutil.mavlink.MAV_CMD_PREFLIGHT_REBOOT_SHUTDOWN,
                                             0, 1, 0, 0, 0, 0, 0, 0)
    finally:
        connection.close()


if __name__ == "__main__":
    main()
