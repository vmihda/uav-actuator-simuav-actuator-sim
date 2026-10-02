#!/usr/bin/env python3
"""Replay terminal commands and assert exact externally visible behavior."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
commands = "\n".join([
    "CMD:DEPLOY", "CMD:START", "CMD:DEPLOY", "TICK:9999", "CMD:DEPLOY", "TICK:1",
    "CMD:DEPLOY", "TICK:3000", "CMD:DEPLOY", "CMD:STOP", "bad", "CMD:STOP",
    "CMD:START", "TICK:30000", "CMD:STOP", ""
])
result = subprocess.run(["rtk", "proxy", sys.executable, str(ROOT / "scripts" / "simulator.py")],
                        input=commands, text=True, capture_output=True, cwd=ROOT, check=True)
states = [line for line in result.stdout.splitlines() if line.startswith("STATE:")]
expected = [("SAFE", 0, 0), ("SAFE", 0, 0), ("ARMING", 10, 0), ("ARMING", 10, 0),
            ("ARMING", 1, 0), ("ARMING", 1, 0), ("ARMED", 0, 0), ("ACTUATED", 0, 0),
            ("ACTUATED", 0, 0), ("ACTUATED", 0, 0), ("SAFE", 0, 0), ("FAULT", 0, 2),
            ("SAFE", 0, 0), ("ARMING", 10, 0), ("FAULT", 0, 3), ("SAFE", 0, 0)]
assert states == [f"STATE:{state},TIME_LEFT:{left},ERR:{error}" for state, left, error in expected], result.stdout
pulses = [line for line in result.stdout.splitlines() if line.startswith("PULSE:")]
assert len(pulses) == len(expected), result.stdout
assert pulses[7] == "PULSE:1,DEPLOYMENTS:1", result.stdout
assert pulses[8] == "PULSE:0,DEPLOYMENTS:1", result.stdout
assert pulses[-1] == "PULSE:0,DEPLOYMENTS:1", result.stdout
print("PASS terminal replay: early activation, countdown, pulse, STOP, invalid input, timeout")
