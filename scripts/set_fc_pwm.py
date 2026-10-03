"""Set the SpeedyBee M1 output pulse width via MAVLink DO_SET_SERVO.

Runtime only: nothing is stored in parameters, a reboot clears it.
"""
import argparse
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / ".pio" / "python-deps"))
from pymavlink import mavutil
from serial.tools import list_ports

SERVO = 1  # M1
ARDUPILOT_USB_VID = 0x1209  # ArduPilot USB vendor ID (pid.codes)


def find_flight_controller():
    # macOS renames the port when the cable moves to another USB socket.
    ports = [port.device for port in list_ports.comports()
             if port.vid == ARDUPILOT_USB_VID and port.device.startswith("/dev/cu.")]
    if not ports:
        raise SystemExit("No ArduPilot flight controller found on USB; pass --port")
    if len(ports) > 1:
        raise SystemExit(f"Several flight controllers found ({', '.join(ports)}); pass --port")
    return ports[0]


def read_output(connection):
    connection.mav.command_long_send(connection.target_system, connection.target_component,
                                     mavutil.mavlink.MAV_CMD_REQUEST_MESSAGE, 0,
                                     mavutil.mavlink.MAVLINK_MSG_ID_SERVO_OUTPUT_RAW, 0, 0, 0, 0, 0, 0)
    message = connection.recv_match(type="SERVO_OUTPUT_RAW", blocking=True, timeout=3)
    return None if message is None else message.servo1_raw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pulse_us", nargs="?", type=int, help="pulse width, 800..2200 us")
    parser.add_argument("--read", action="store_true", help="only report the current M1 output")
    parser.add_argument("--port", help="flight-controller serial port (default: auto-detect)")
    args = parser.parse_args()
    if args.read == (args.pulse_us is not None):
        parser.error("give either a pulse width or --read")
    if args.pulse_us is not None and not 800 <= args.pulse_us <= 2200:
        parser.error("pulse width must be within 800..2200 us")
    connection = mavutil.mavlink_connection(args.port or find_flight_controller(), baud=115200,
                                            source_system=255)
    try:
        heartbeat = connection.wait_heartbeat(timeout=10)
        if not heartbeat:
            raise SystemExit("No heartbeat from the flight controller")
        if heartbeat.base_mode & mavutil.mavlink.MAV_MODE_FLAG_SAFETY_ARMED:
            raise SystemExit("Flight controller is armed; refusing to touch outputs")
        if args.pulse_us is not None:
            connection.mav.command_long_send(connection.target_system, connection.target_component,
                                             mavutil.mavlink.MAV_CMD_DO_SET_SERVO, 0,
                                             SERVO, args.pulse_us, 0, 0, 0, 0, 0)
            ack = connection.recv_match(type="COMMAND_ACK", blocking=True, timeout=3)
            result = "no ACK" if ack is None else mavutil.mavlink.enums["MAV_RESULT"][ack.result].name
            print(f"DO_SET_SERVO M1={args.pulse_us}us -> {result}")
            time.sleep(0.3)
        print(f"FC reports M1 output: {read_output(connection)} us")
    finally:
        connection.close()


if __name__ == "__main__":
    main()
