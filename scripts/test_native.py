#!/usr/bin/env python3
"""Compile and execute the portable firmware contract tests, without hardware."""
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise SystemExit("A C++ compiler (clang++ or g++) is required")
    output = ROOT / ".pio" / "native"
    output.mkdir(parents=True, exist_ok=True)
    executable = output / "contract-tests"
    sources = ["test/native/test_main.cpp", "src/fsm.cpp", "src/uart_protocol.cpp",
               "src/indicator_pattern.cpp", "src/uart_handler.cpp",
               "src/web_server.cpp", "src/indicators.cpp", "src/event_log.cpp",
               "src/main.cpp"]
    subprocess.run([compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-pedantic", "-fsanitize=address,undefined", "-g", "-DENABLE_USB_COMMANDS=1",
                    "-I", str(ROOT / "test" / "support"),
                    "-I", str(ROOT / "include"), *[str(ROOT / p) for p in sources],
                    "-o", str(executable)], check=True, cwd=ROOT)
    subprocess.run([str(executable)], check=True, cwd=ROOT)


if __name__ == "__main__":
    main()
