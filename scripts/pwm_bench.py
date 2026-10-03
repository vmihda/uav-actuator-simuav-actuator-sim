#!/usr/bin/env python3
"""Drive the simulator over PWM only: FC M1 -> ESP32 GPIO27.

The flight controller output is set with MAVLink DO_SET_SERVO; ESP32 USB
telemetry is only read, never written, so the arming survives on the PWM
heartbeat alone. Requires the esp32dev-test firmware (10 s arming).
"""
import argparse
from pathlib import Path
import re
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / ".pio" / "python-deps"))
import serial
from pymavlink import mavutil
from set_fc_pwm import find_flight_controller

STATE = re.compile(r"STATE:([A-Z]+),TIME_LEFT:(\d+),ERR:(\d+)")


class Esp32Telemetry:
    def __init__(self, port):
        self.port = serial.Serial(port=None, baudrate=115200, timeout=0.2)
        self.port.dtr = False
        self.port.rts = False
        self.port.port = port
        self.port.open()
        self.state = None
        self.lines = []

    def poll(self):
        line = self.port.readline().decode(errors="replace").strip()
        if not line:
            return None
        self.lines.append(line)
        match = STATE.search(line)
        if match:
            self.state = (match.group(1), int(match.group(2)), int(match.group(3)))
        return line

    def wait_state(self, name, timeout, forbid=()):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.poll()
            if self.state and self.state[0] in forbid:
                raise AssertionError(f"Unexpected {self.state} while waiting for {name}")
            if self.state and self.state[0] == name:
                print(f"STATE:{name},TIME_LEFT:{self.state[1]},ERR:{self.state[2]}", flush=True)
                return self.state
        raise AssertionError(f"Expected {name} within {timeout}s, last {self.state}")

    def wait_line(self, text, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self.poll()
            if line and text in line:
                print(line, flush=True)
                return
        raise AssertionError(f"Expected a line containing {text!r} within {timeout}s")

    def hold(self, name, seconds):
        """Asserts the state stays `name` for the given time."""
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            self.poll()
            if self.state and self.state[0] != name:
                raise AssertionError(f"Left {name}: {self.state}")
        print(f"Held {name} for {seconds}s", flush=True)

    def close(self):
        self.port.close()


class FlightControllerOutput:
    def __init__(self, port, servo):
        self.servo = servo
        self.connection = mavutil.mavlink_connection(port, baud=115200, source_system=255)
        heartbeat = self.connection.wait_heartbeat(timeout=10)
        if not heartbeat:
            raise TimeoutError("No flight-controller heartbeat")
        if heartbeat.base_mode & mavutil.mavlink.MAV_MODE_FLAG_SAFETY_ARMED:
            raise RuntimeError("Flight controller is armed; PWM bench cancelled")

    def set(self, pulse_us):
        self.connection.mav.command_long_send(
            self.connection.target_system, self.connection.target_component,
            mavutil.mavlink.MAV_CMD_DO_SET_SERVO, 0, self.servo, pulse_us, 0, 0, 0, 0, 0)
        ack = self.connection.recv_match(type="COMMAND_ACK", blocking=True, timeout=3)
        if ack is None or ack.result != mavutil.mavlink.MAV_RESULT_ACCEPTED:
            raise RuntimeError(f"DO_SET_SERVO {pulse_us}us was not accepted")
        print(f"M{self.servo} -> {pulse_us}us", flush=True)

    def close(self):
        self.connection.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fc-port", help="flight-controller port (default: auto-detect)")
    parser.add_argument("--esp-port", default="/dev/cu.usbserial-A5069RR4")
    parser.add_argument("--servo", type=int, default=1, help="flight-controller output number (M1 = 1)")
    args = parser.parse_args()
    esp = Esp32Telemetry(args.esp_port)
    fc = FlightControllerOutput(args.fc_port or find_flight_controller(), args.servo)
    try:
        esp.wait_state("SAFE", 3)
        fc.set(1000)
        esp.wait_line("CAPTURED:1,NEUTRAL:1", 3)
        fc.set(1500)
        arming = esp.wait_state("ARMING", 3)
        if arming[1] > 30:
            raise RuntimeError("PWM bench expects the esp32dev-test build, not five-minute arming")
        esp.wait_state("ARMED", arming[1] + 3, forbid=("FAULT",))
        # Longer than the 30 s control timeout: only the PWM heartbeat keeps it armed.
        esp.hold("ARMED", 35)
        fc.set(2000)
        esp.wait_state("ACTUATED", 3)
        fc.set(1000)
        esp.wait_state("SAFE", 3)
        fc.set(1500)
        arming = esp.wait_state("ARMING", 3)
        fc.set(2000)
        esp.wait_line("PWM:CMD:DEPLOY", 3)
        esp.wait_state("ARMED", arming[1] + 3, forbid=("FAULT", "ACTUATED"))
        esp.hold("ARMED", 5)
        fc.set(1000)
        esp.wait_state("SAFE", 3)
        print("PASS PWM bench: neutral, arming, PWM heartbeat, deploy, stop, consumed early deploy")
    finally:
        try:
            fc.set(1000)
        finally:
            fc.close()
            esp.close()


if __name__ == "__main__":
    main()
