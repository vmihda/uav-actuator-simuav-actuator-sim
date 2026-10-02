#!/usr/bin/env python3
"""Exercise the software-only simulator over USB or ArduPilot UART6 forwarding."""
import argparse
from pathlib import Path
import re
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / ".pio" / "python-deps"))
import serial

STATE = re.compile(r"STATE:([A-Z]+),TIME_LEFT:(\d+),ERR:(\d+)")


class UsbTransport:
    def __init__(self, port):
        self.port = serial.Serial(port=None, baudrate=115200, timeout=0.2)
        self.port.dtr = False
        self.port.rts = False
        self.port.port = port
        self.port.open()

    def exchange(self, text):
        self.port.reset_input_buffer()
        self.port.write(text.encode())
        deadline = time.monotonic() + 1.2
        output = ""
        while time.monotonic() < deadline:
            output += self.port.readline().decode(errors="replace")
            if STATE.search(output):
                return output
        return output

    def close(self):
        self.port.close()


class FcTransport:
    def __init__(self, port):
        from pymavlink import mavutil
        self.mavutil = mavutil
        self.connection = mavutil.mavlink_connection(port, baud=115200, source_system=255)
        heartbeat = self.connection.wait_heartbeat(timeout=10)
        if not heartbeat:
            raise TimeoutError("No flight-controller heartbeat")
        if heartbeat.base_mode & mavutil.mavlink.MAV_MODE_FLAG_SAFETY_ARMED:
            raise RuntimeError("Flight controller is armed; bench test cancelled")

    def exchange(self, text):
        data = list(text.encode())
        if len(data) > 70:
            raise ValueError("SERIAL_CONTROL payload exceeds 70 bytes")
        mavlink = self.mavutil.mavlink
        self.connection.mav.serial_control_send(
            106, mavlink.SERIAL_CONTROL_FLAG_RESPOND | mavlink.SERIAL_CONTROL_FLAG_EXCLUSIVE,
            300, 115200, len(data), data + [0] * (70 - len(data)))
        deadline = time.monotonic() + 1.5
        output = ""
        while time.monotonic() < deadline:
            message = self.connection.recv_match(type="SERIAL_CONTROL", blocking=True, timeout=0.3)
            if message and message.device == 106:
                output += bytes(message.data[:message.count]).decode(errors="replace")
                if STATE.search(output):
                    return output
        return output

    def close(self):
        # Release exclusive runtime access; no persistent parameters are changed.
        self.connection.mav.serial_control_send(106, 0, 0, 0, 0, [0] * 70)
        self.connection.close()


def expect(transport, command, state, error=0, timeout=3):
    deadline = time.monotonic() + timeout
    output = transport.exchange(command + "\nCMD:STATUS\n")
    while True:
        matches = list(STATE.finditer(output))
        if matches:
            latest = matches[-1]
            if latest.group(1) == state and int(latest.group(3)) == error:
                print(latest.group(0), flush=True)
                return int(latest.group(2))
        if time.monotonic() >= deadline:
            raise AssertionError(f"Expected {state}/ERR:{error}, received {output!r}")
        output = transport.exchange("CMD:STATUS\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--via-fc", action="store_true")
    args = parser.parse_args()
    transport = FcTransport(args.port) if args.via_fc else UsbTransport(args.port)
    try:
        expect(transport, "CMD:STOP", "SAFE")
        expect(transport, "CMD:DEPLOY", "SAFE")
        left = expect(transport, "CMD:START", "ARMING")
        if left > 30:
            raise RuntimeError("Hardware smoke expects the short test build, not five-minute arming")
        expect(transport, "CMD:DEPLOY", "ARMING")
        expect(transport, "CMD:STATUS", "ARMED", timeout=left + 3)
        expect(transport, "CMD:DEPLOY", "ACTUATED")
        time.sleep(3.1)
        expect(transport, "CMD:DEPLOY", "ACTUATED")
        expect(transport, "CMD:STOP", "SAFE")
        expect(transport, "INVALID", "FAULT", error=2)
        expect(transport, "CMD:STOP", "SAFE")
        print("PASS hardware bench sequence" + (" through ArduPilot UART6" if args.via_fc else " over USB"))
    finally:
        try:
            transport.exchange("CMD:STOP\n")
        finally:
            transport.close()


if __name__ == "__main__":
    main()
