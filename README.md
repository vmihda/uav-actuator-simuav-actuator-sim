# ESP32 Actuator Simulator

PlatformIO/Arduino implementation of the supplied UART + Wi-Fi recovery-controller
simulation. **There is no actuator power GPIO.** Deployment changes a logical
flag and drives the indication pattern for three seconds.

## Connections

| ESP32 | Flight-controller UART6 |
| --- | --- |
| GPIO16 / RX2 | T6 / TX |
| GPIO17 / TX2 | R6 / RX |
| GPIO27 (PWM input) | M1 |
| GND | GND |

M1 is the third contact of the 8-pin ESC connector counting from `G`
(`G V 1 2 3 4 C T`). Never connect `V` (battery). Run the flight controller from
USB only, without a battery. M1 carries 50 Hz, 3.3V PWM.

UART2 uses 115200 baud, 8N1 and 3.3V logic. USB is UART0 and remains available for
diagnostics. GPIO2 drives an active-high LED. An optional **active** buzzer can
mirror the LED using `-DBUZZER_PIN=<unused GPIO>`; it is disabled by default.

The on-board BOOT button (GPIO0) is a local STOP. A button held from power-on
keeps the ESP32 in its ROM bootloader, so the application never starts.

The flight controller must send the specified ASCII commands. Merely enabling
MSP, MAVLink or another binary protocol on UART6 does not generate these commands.
Binary/unknown input is deliberately treated as invalid input by this simulator.

## Build and upload

Requires PlatformIO Core and its ESP32 toolchain. The build helper keeps writable
PlatformIO state inside `.pio` and reuses installed tools.

```sh
python3 scripts/build_firmware.py
```

This builds `esp32dev` (300-second arming interval), `esp32dev-test` (10 seconds)
and a LittleFS image. Compiler warnings are treated as errors.

First upload the firmware **and** filesystem to your ESP32. Replace the serial
port with the one assigned to your board:

```sh
python3 scripts/build_firmware.py --environment esp32dev-test --upload --port /dev/cu.usbserial-A5069RR4
```

The upload command also installs `data/info.txt` and `data/settings.json` into
LittleFS. It replaces the filesystem contents, including the event journal; use it
for first provisioning and whenever the settings change. Subsequent firmware-only
uploads should preserve the event journal:

```sh
python3 scripts/build_firmware.py --environment esp32dev-test --firmware-only --upload --port /dev/cu.usbserial-A5069RR4
```

Use `esp32dev` instead for the normal five-minute delay. Test firmware additionally
accepts the UART command strings through USB for bench verification. Normal
firmware accepts commands through UART2 and HTTP; USB reports status only.

## Wi-Fi panel and API

Connect to **ACTUATOR-SIM**, password **password123**, and open
**http://192.168.4.1**. The panel refreshes once per second. Deploy becomes
available in ARMED. Failed status requests disable the command buttons.

| Endpoint | Method | Action |
| --- | --- | --- |
| `/` | GET | Manual test panel |
| `/status` | GET | Status and control heartbeat |
| `/start` | POST | SAFE → ARMING |
| `/stop` | POST | Disarm, cancel pulse or retry fault recovery |
| `/deploy` | POST | ARMED → ACTUATED |

Successful commands return HTTP 200; disallowed commands return HTTP 409 with
current status and `accepted:false`. If the controller does not confirm a command
within one second, the reply is HTTP 503 `{"error":"command_outcome_unknown"}`:
the command may still have executed, so the panel locks its buttons until the
next status refresh. Wrong methods return HTTP 405. JSON example:

```json
{"state":"ARMING","time_left":10,"err":0,"pulse_active":false,"deployment_count":0,"accepted":true,"arming_seconds":10,"simulation":true}
```

`deployment_count` counts successful deployments during the current boot.

## UART protocol and states

Send LF-terminated commands (CRLF also works):

```text
CMD:START
CMD:STOP
CMD:DEPLOY
CMD:STATUS
```

UART2 sends telemetry every second and immediately for STATUS:

```text
STATE:ARMING,TIME_LEFT:9,ERR:0
```

The parser accepts at most 63 bytes before LF. It waits for complete lines,
rejects unknown commands/control bytes, and discards an oversized line through
its terminating LF. Blank lines are ignored. Each loop processes at most 128
received bytes so a continuous stream cannot monopolize the application.

- **POST:** runs the self-test below. The simulated output starts off. A failed
  check enters FAULT.
- **SAFE:** output off. START begins a fresh arming interval.
- **ARMING:** early DEPLOY is rejected without resetting the countdown. Repeated
  START is rejected. STOP returns to SAFE.
- **ARMED:** DEPLOY is accepted once; STOP disarms.
- **ACTUATED:** logical output on for 3 seconds, then off. The state remains
  latched until STOP. Repeated DEPLOY cannot restart the pulse.
- **FAULT:** output off. STOP reruns health checks and clears the fault only if
  they pass. Unknown commands, UART overflow and control timeout enter FAULT.

While ARMING/ARMED, send a valid command or STATUS **more often than every 30
seconds**. HTTP status polling and a captured, neutral PWM signal also supply
this heartbeat, so keep the panel open when using Wi-Fi only. Outgoing telemetry does not count as incoming control.
An expired deadline is evaluated before a late command; late DEPLOY cannot
prevent a timeout. SAFE and latched ACTUATED do not require a heartbeat.

## PWM commands

GPIO27 measures every pulse. Default bands (inclusive, from `settings.json`):

| Width | Meaning |
| --- | --- |
| below 800 µs or above 2200 µs | invalid |
| 900–1300 µs | STOP |
| 1400–1600 µs | START |
| 1700–2100 µs | DEPLOY |
| anything between | dead zone |

- A command fires once when the width stays in its band for 200 ms; it repeats
  only after another command band. Dead zones never repeat a command.
- 200 ms of valid pulses capture the signal. The band present at capture is a
  starting position and commands nothing.
- START and DEPLOY need *neutral*: the width must have passed through STOP.
  When the FSM faults, neutral is kept only if PWM is at STOP.
- A DEPLOY sent too early (ARMING) is rejected and consumed; it does not fire
  later when ARMED.
- No pulse, or only invalid widths, for 500 ms releases the capture. In
  ARMING/ARMED this enters FAULT `PwmLost` or `PwmInvalid`.

## Settings

`data/settings.json` is installed into LittleFS and validated strictly at boot:
every field is required, unknown keys and wrong JSON types are rejected, PWM bands
must be ordered with non-empty dead zones, and pins must not collide with UART2,
the LED or each other. `pins.button` may be `-1` to disable the button. A missing
or invalid file enters FAULT `SettingsInvalid`; the USB log names the field, for
example `POST:SETTINGS:FAIL:pwm.start.min_us must exceed pwm.stop.max_us`.
Settings are read once at boot.

## Self-test and supply monitoring

Boot logs one USB line per check: `POST:<CHECK>:OK`, `FAIL:<detail>` or
`SKIPPED`, after `DEVICE:MAC=…,CHIP=…,REV=…,FLASH_MB=…`.

| Check | Verifies | Fault |
| --- | --- | --- |
| `FS` | LittleFS marker, write/read probe, journal paths | 5 |
| `SETTINGS` | `/settings.json` | 6 |
| `CHIP` | Factory MAC (eFuse CRC), ESP32 model, flash ≥ 4 MB | 1 |
| `HEAP` | At least 64 KiB free | 1 |
| `BUTTON` | Not held for 500 ms during the boot window | 7 |
| `POWER` | Supply verdict | 10 or 11 |
| `PWM` | Edge interrupt registered (no signal is not an error) | 1 |
| `LINKS` | UART2, SoftAP and watchdog | 1 |

STOP in FAULT reruns these checks, except `CHIP` and settings parsing.

The supply voltage is read through a sensor interface. It is **simulated** until
a VIN divider is fitted: valid range 4.5–5.5 V, default 5.0 V. Outside the range
for one second enters FAULT `PowerOutOfRange`; unreadable values enter
`SensorFailure`. The test firmware accepts `SIM:VIN:<mV>` over USB
(0–30000) and replies `SIM:VIN:<mV>:OK`.

## Errors, event journal and indication

| ERR | Meaning |
| --- | --- |
| 0 | None |
| 1 | Peripheral/watchdog/self-test failure |
| 2 | Invalid command |
| 3 | Control timeout |
| 4 | UART line overflow |
| 5 | LittleFS or deployment-journal failure |
| 6 | Settings missing or invalid |
| 7 | Button stuck |
| 8 | PWM signal invalid |
| 9 | PWM signal lost |
| 10 | Supply voltage out of range |
| 11 | Sensor failure |

LittleFS is mounted with automatic formatting disabled. An unprovisioned or
damaged filesystem causes FAULT rather than erasing data. Events are appended
**before** beginning a deployment pulse:

```text
UPTIME_MS:10000,STATE:ACTUATED,COUNT:1
```

`/events.log` rotates at 4096 bytes to `/events.previous.log`. The journal survives
reset and STOP; timestamps and counters are relative to each boot. A failed
write blocks the pulse and causes error 5.

Indication is driven by timestamps without `delay()`: SAFE 0.5Hz, ARMING 2Hz,
ARMED 5Hz, ACTUATED continuously on for 3 seconds, FAULT three 100ms flashes with
a pause. A five-second ESP32 task watchdog monitors the main loop.

The HTTP server runs on a separate FreeRTOS task on core 0 at idle priority;
FreeRTOS time slicing keeps the idle watchdog serviced even if the vendor HTTP
parser waits for a slow request. The main loop alone owns the FSM and continues
servicing UART/timers/indication. HTTP commands pass through a one-slot queue
with a one-second deadline; replies use a protected immutable status snapshot.

## Tests and terminal simulation

Host contract tests compile the actual firmware sources with deterministic
hardware adapters, AddressSanitizer/UndefinedBehaviorSanitizer and strict warnings.
They cover FSM, timers/rollover, parser, UART, HTTP, indication, persistent event
logging, full startup and fault recovery.

```sh
python3 scripts/test_native.py
python3 scripts/test_simulator.py
node --test test/web/panel.test.cjs
python3 scripts/simulator.py
```

The terminal simulator uses the real FSM/parser and a manual clock. Enter:

```text
CMD:DEPLOY
CMD:START
CMD:DEPLOY
TICK:10000
CMD:DEPLOY
TICK:3000
CMD:STOP
FAULT
CMD:STOP
```

`TICK:<ms>` advances simulated time without waiting. `FAULT` injects a recoverable
error. Pass `--normal` to use 300 seconds; advance in increments below 30 seconds
and send STATUS between them to maintain the control heartbeat.

Typical compiled resource usage: about 45KB static RAM and 830KB application
flash (PlatformIO reports exact values for each build). The HTML lives in flash,
protocol/status buffers have fixed capacities, and status polling does not write
to flash.

## SpeedyBee F405 V3 / ArduPilot bench connection

The connected board was identified as ArduPilot 4.7.1. On this exact board,
R6/T6 are `SERIAL6` ([official board documentation](https://ardupilot.org/copter/docs/common-speedybeef4-v3.html)).
The requested GPS-free UART configuration is:

| Parameter | Value |
| --- | --- |
| `SERIAL6_PROTOCOL` | `0` |
| `SERIAL6_BAUD` | `115` (115200 baud) |
| `SERIAL6_OPTIONS` | `0` |
| `GPS1_TYPE` | `0` |
| `GPS2_TYPE` | `0` |

Protocol `0` initializes the raw UART without creating a GPS or MAVLink driver.
In this firmware, `-1` disables the RX/TX pins, so it cannot be used for raw
forwarding after reboot ([4.7.1 SerialManager source](https://raw.githubusercontent.com/ArduPilot/ardupilot/Copter-4.7.1/libraries/AP_SerialManager/AP_SerialManager.cpp)).
Protocol and GPS changes require a reboot. The original parameter values are
preserved in `backups/fc-parameters-before.json`, outside the disposable `.pio`
directory; the previous ESP32 flash image is `backups/esp32-before-simulator.bin`.

The supplied helper uses MAVLink `SERIAL_CONTROL` device `106` to send the exact
ASCII commands from the laptop through ArduPilot and UART6. This is a manual
bench-control path; it does not enable automatic aircraft recovery behavior.
SpeedyBee F405 V3 firmware has no onboard Lua scripting
([firmware feature manifest](https://firmware.ardupilot.org/Copter/stable/speedybeef4v3/features.txt)).

For these helpers, use the PlatformIO Python environment with pyserial and install
pymavlink into the project-local dependency directory:

```sh
~/.platformio/penv/bin/pip install --target .pio/python-deps pymavlink
~/.platformio/penv/bin/python scripts/inspect_fc.py
~/.platformio/penv/bin/python scripts/configure_fc.py --apply --reboot
~/.platformio/penv/bin/python scripts/hardware_smoke.py --via-fc --port /dev/cu.usbmodem2101
~/.platformio/penv/bin/python scripts/hardware_smoke.py --port /dev/cu.usbserial-A5069RR4
```

The bench test exercises early deployment rejection, arming, activation,
invalid input and STOP recovery, then leaves the simulator SAFE. Runtime
exclusive UART access is released afterward. Configuration/testing helpers
refuse to run the bench operation while the flight controller is armed.

The PWM bench drives M1 with `DO_SET_SERVO` and only reads ESP32 telemetry, so
the 35-second ARMED hold proves the PWM heartbeat. It covers neutral, arming,
deployment, STOP and a consumed early DEPLOY, then leaves M1 at 1000 µs:

```sh
~/.platformio/penv/bin/python scripts/pwm_bench.py
```

Pulling the GPIO27 wire while ARMING is a manual check for `PwmLost` (ERR 9).

## Modules

`fsm` owns transitions and deadlines; `uart_protocol` owns framing/serialization;
`uart_handler` connects UART2; `web_server` serves the panel/API; `indicator_pattern`
and `indicators` implement patterns/GPIO; `event_log` validates/persists LittleFS;
`settings` parses `settings.json`; `pwm_decoder` turns widths into commands and
`pwm_input` captures them; `sensor`, `simulated_sensor` and `power_monitor`
monitor the supply; `button_monitor` debounces BOOT; `self_test` and
`device_info` implement POST; `main` wires them and shares command dispatch.
Hardware/timing constants are in `include/config.h`.
