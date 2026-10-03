"""Set the SpeedyBee M1 output pulse width via MAVLink DO_SET_SERVO.

Usage: set_fc_pwm.py <pulse_us> | --read
Runtime only: nothing is stored in parameters, a reboot clears it.
"""
import sys
import time

from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / ".pio" / "python-deps"))
from pymavlink import mavutil

PORT = "/dev/cu.usbmodem2101"
SERVO = 1  # M1


def read_output(connection):
    connection.mav.command_long_send(connection.target_system, connection.target_component,
                                     mavutil.mavlink.MAV_CMD_REQUEST_MESSAGE, 0,
                                     mavutil.mavlink.MAVLINK_MSG_ID_SERVO_OUTPUT_RAW, 0, 0, 0, 0, 0, 0)
    message = connection.recv_match(type="SERVO_OUTPUT_RAW", blocking=True, timeout=3)
    return None if message is None else message.servo1_raw


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    connection = mavutil.mavlink_connection(PORT, baud=115200, source_system=255)
    try:
        heartbeat = connection.wait_heartbeat(timeout=10)
        if not heartbeat:
            raise SystemExit("No heartbeat from the flight controller")
        if heartbeat.base_mode & mavutil.mavlink.MAV_MODE_FLAG_SAFETY_ARMED:
            raise SystemExit("Flight controller is armed; refusing to touch outputs")
        if sys.argv[1] != "--read":
            pulse = int(sys.argv[1])
            if not 800 <= pulse <= 2200:
                raise SystemExit("Pulse must be within 800..2200 us")
            connection.mav.command_long_send(connection.target_system, connection.target_component,
                                             mavutil.mavlink.MAV_CMD_DO_SET_SERVO, 0,
                                             SERVO, pulse, 0, 0, 0, 0, 0)
            ack = connection.recv_match(type="COMMAND_ACK", blocking=True, timeout=3)
            result = "no ACK" if ack is None else mavutil.mavlink.enums["MAV_RESULT"][ack.result].name
            print(f"DO_SET_SERVO M1={pulse}us -> {result}")
            time.sleep(0.3)
        print(f"FC reports M1 output: {read_output(connection)} us")
    finally:
        connection.close()


if __name__ == "__main__":
    main()
