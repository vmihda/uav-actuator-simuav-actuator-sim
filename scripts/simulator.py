#!/usr/bin/env python3
"""Run the real portable FSM/parser in a deterministic terminal simulator."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--normal", action="store_true", help="use 300 seconds instead of 10")
    args = parser.parse_args()
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise SystemExit("A C++ compiler is required")
    output = ROOT / ".pio" / "native"
    output.mkdir(parents=True, exist_ok=True)
    executable = output / "simulator"
    subprocess.run([compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-DARMING_DELAY_MS=" + ("300000UL" if args.normal else "10000UL"),
                    "-I", str(ROOT / "include"), str(ROOT / "tools" / "simulator.cpp"),
                    str(ROOT / "src" / "fsm.cpp"), str(ROOT / "src" / "uart_protocol.cpp"),
                    "-o", str(executable)], cwd=ROOT, check=True)
    subprocess.run([str(executable)], cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
