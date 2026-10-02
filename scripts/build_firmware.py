#!/usr/bin/env python3
"""Build/upload firmware with PlatformIO's writable state inside the project."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def platformio_command():
    executable = shutil.which("pio") or shutil.which("platformio")
    if not executable:
        candidate = Path.home() / ".platformio" / "penv" / "bin" / "pio"
        if candidate.is_file():
            executable = str(candidate)
    if not executable:
        raise SystemExit("PlatformIO Core is required")
    return executable


def build_environment():
    environment = os.environ.copy()
    installed = Path(environment.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    core = ROOT / ".pio" / "pio-core"
    # Reuse installed tools read-only; package-manager locks live in our workspace.
    for directory in ("packages", "platforms"):
        destination = core / directory
        destination.mkdir(parents=True, exist_ok=True)
        source = installed / directory
        if source.is_dir() and source.resolve() != destination.resolve():
            for package in source.iterdir():
                target = destination / package.name
                if package.is_dir() and not target.exists():
                    if directory == "platforms":
                        shutil.copytree(package, target)
                    else:
                        target.symlink_to(package.resolve(), target_is_directory=True)
    environment["PLATFORMIO_CORE_DIR"] = str(core)
    environment["PLATFORMIO_SETTING_ENABLE_TELEMETRY"] = "no"
    environment["PLATFORMIO_CACHE_DIR"] = str(ROOT / ".pio" / "cache")
    return environment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--environment", action="append", choices=["esp32dev", "esp32dev-test"])
    parser.add_argument("--upload", action="store_true")
    parser.add_argument("--port")
    parser.add_argument("--filesystem-only", action="store_true")
    parser.add_argument("--firmware-only", action="store_true", help="preserve existing LittleFS journal")
    args = parser.parse_args()
    if args.filesystem_only and args.firmware_only:
        parser.error("choose either filesystem-only or firmware-only")
    if args.upload and (not args.port or not args.environment or len(args.environment) != 1):
        parser.error("upload requires --port and exactly one --environment")
    environments = args.environment or ["esp32dev", "esp32dev-test"]
    command = [platformio_command(), "run"]
    for name in environments:
        command += ["-e", name]
    environment = build_environment()
    if not args.filesystem_only:
        action = command + (["-t", "upload", "--upload-port", args.port] if args.upload else [])
        subprocess.run(action, cwd=ROOT, env=environment, check=True)
    if args.firmware_only:
        return
    filesystem = [platformio_command(), "run", "-e", environments[0],
                  "-t", "uploadfs" if args.upload else "buildfs"]
    if args.upload:
        filesystem += ["--upload-port", args.port]
    subprocess.run(filesystem, cwd=ROOT, env=environment, check=True)


if __name__ == "__main__":
    main()
