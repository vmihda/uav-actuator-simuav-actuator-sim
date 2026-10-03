# ESP32 Actuator Simulator

Firmware for an ESP32 that pretends to be the controller of a one-shot actuator,
such as a parachute release. It arms on command, waits out a safety timer, and on
deployment turns a logical output on for 3 seconds. Nothing is ever powered.
The "output" is a flag in memory that you see on the LED and in the logs.

You can drive it four ways: PWM from a flight controller, a web page over the
ESP32's own Wi-Fi, ASCII commands on UART2, or the BOOT button (stop only). Bad
input, a lost signal or a failed self-test puts it in FAULT, where nothing can arm
or deploy until a STOP passes the self-test again.

## What you need

- An ESP32 dev board (tested on ESP32-D0WD-V3 with 4 MB flash) and a USB cable.
- [PlatformIO Core](https://platformio.org/install) in `~/.platformio`. Python 3
  runs the helper scripts. Node.js is only needed for the web panel tests.
- Optional, for PWM and UART control: a SpeedyBee F405 V3 running ArduPilot, its
  8-pin ESC cable and four jumper wires.

## Wiring

| ESP32 | SpeedyBee F405 V3 |
| --- | --- |
| GPIO16 (RX2) | T6 |
| GPIO17 (TX2) | R6 |
| GPIO27 (PWM in) | M1 |
| GND | G |

M1 sits on the 8-pin ESC connector, marked `G V 1 2 3 4 C T` on the board. Count
from `G`: the second contact is `V` (battery), the third is M1. Do not connect
`V`, and keep the battery off; the flight controller runs from USB for the bench.
M1 carries 50 Hz, 3.3 V PWM, safe to wire straight into the ESP32.

On the ESP32 itself, GPIO2 drives the LED and the BOOT button (GPIO0) acts as STOP.
Holding BOOT while the board powers up starts the ROM bootloader instead of the
firmware, which is harmless but looks like a dead board.

## Build and flash

There are two firmware builds. `esp32dev` is the real one, with the five-minute
safety timer. `esp32dev-test` arms in 10 seconds and also accepts commands typed
over USB, which the bench scripts rely on.

Build both, plus the filesystem image:

```sh
python3 scripts/build_firmware.py
```

The first time, flash firmware and filesystem together. Replace the port with
yours (`~/.platformio/penv/bin/pio device list` shows it):

```sh
python3 scripts/build_firmware.py --environment esp32dev-test --upload --port /dev/cu.usbserial-A5069RR4
```

The filesystem image carries `data/info.txt` and `data/settings.json`. Uploading
it erases everything on the filesystem, including the deployment journal. After
the first time, flash only the firmware unless you changed the settings:

```sh
python3 scripts/build_firmware.py --environment esp32dev --firmware-only --upload --port /dev/cu.usbserial-A5069RR4
```

## Check that it booted

Open the serial monitor. `--dtr 0 --rts 0` keeps the monitor from holding the
board in reset:

```sh
~/.platformio/penv/bin/pio device monitor -e esp32dev-test -p /dev/cu.usbserial-A5069RR4 -b 115200 --dtr 0 --rts 0
```

A healthy start looks like this:

```text
DEVICE:MAC=A4:F0:0F:67:69:EC,CHIP=ESP32-D0WD-V3,REV=3,FLASH_MB=4
POST:FS:OK
POST:SETTINGS:OK
POST:CHIP:OK
POST:HEAP:OK
POST:BUTTON:OK
POST:POWER:OK
POST:PWM:OK
POST:LINKS:OK
STATE:SAFE,TIME_LEFT:0,ERR:0
```

Any `POST:<CHECK>:FAIL:<reason>` line explains what is wrong, and the board stays
in FAULT. With the test build, don't type into the monitor unless you mean it:
every line you send is a command.

A line like `[E][vfs_api.cpp:105] open(): /littlefs/events.previous.log does not
exist` is noise from the Arduino LittleFS library when a file is absent. Ignore it.

## The web panel

1. Join the Wi-Fi network `ACTUATOR-SIM` (password `password123`). It has no
   internet access, so your phone may complain; stay connected anyway.
2. Open http://192.168.4.1.

The page shows the state, the arming countdown, whether the simulated pulse is on
and the current error. It refreshes every second.

| Button | Works in | Does |
| --- | --- | --- |
| Start (Arm) | SAFE | Starts the safety timer (ARMING) |
| Disarm (Stop) | any state | Back to SAFE; in FAULT it reruns the self-test first |
| Deploy (Activate) | ARMED | 3-second simulated pulse, then stays ACTUATED until Stop |

Keep the page open and in the foreground while arming. Its once-a-second refresh
is the heartbeat; without any heartbeat for 30 seconds the board faults with ERR 3.

The same endpoints work from a terminal:

```sh
curl http://192.168.4.1/status
curl -X POST http://192.168.4.1/start
curl -X POST http://192.168.4.1/deploy
curl -X POST http://192.168.4.1/stop
```

```json
{"state":"ARMING","time_left":10,"err":0,"pulse_active":false,"deployment_count":0,"accepted":true,"arming_seconds":10,"simulation":true}
```

A command allowed in the current state returns 200. A refused one returns 409 with
`"accepted":false` and the current status. 503 `command_outcome_unknown` means the
firmware didn't answer within a second; the command may still have run, so check
`/status`. GET on a command URL returns 405 and changes nothing.

## Commanding over PWM

This is how a flight controller would drive it. The pulse width picks the command:

| Pulse width | Command |
| --- | --- |
| 900–1300 µs | STOP |
| 1400–1600 µs | START |
| 1700–2100 µs | DEPLOY |
| between those bands | nothing (dead zone) |
| under 800 or over 2200 µs | invalid signal |

Without an RC transmitter, set the width on SpeedyBee output M1 from the laptop
over MAVLink. The script finds the flight controller on USB by itself (`--port`
overrides it). The setting is not stored and disappears when the flight
controller reboots:

```sh
~/.platformio/penv/bin/python scripts/set_fc_pwm.py 1000   # STOP, also "neutral"
~/.platformio/penv/bin/python scripts/set_fc_pwm.py 1500   # START
~/.platformio/penv/bin/python scripts/set_fc_pwm.py 2000   # DEPLOY
~/.platformio/penv/bin/python scripts/set_fc_pwm.py --read # what M1 outputs now
```

Rules worth knowing before you test:

- A width has to hold for 200 ms before it counts, and each band fires once. Holding
  START does not restart the timer; you have to leave the band and come back.
- Whatever band the signal is in when the ESP32 first sees it is treated as a
  starting position and commands nothing.
- START and DEPLOY only work after the width has passed through STOP. After a
  fault the same applies again, unless the signal was already sitting at STOP.
- A DEPLOY sent during ARMING is refused and used up. When the timer finishes, the
  board goes to ARMED and waits; it does not deploy on its own.
- While the signal is valid it counts as the heartbeat, so a flight controller can
  hold START through the whole five minutes.
- If pulses stop, or every pulse is out of range, for 500 ms during ARMING or ARMED,
  the board faults with ERR 9 (lost) or ERR 8 (invalid).

The serial log shows what the decoder sees, for example
`PWM:1501us,BAND:START,CAPTURED:1,NEUTRAL:1` and `PWM:CMD:START`.

## Commands over UART2 and USB

UART2 accepts these lines (LF or CRLF):

```text
CMD:START
CMD:STOP
CMD:DEPLOY
CMD:STATUS
```

It answers STATUS at once and also sends telemetry every second:
`STATE:ARMING,TIME_LEFT:9,ERR:0`. Any other line, a control byte or a line over
63 characters is a fault. The test build accepts the same commands over USB, plus
`SIM:VIN:<millivolts>` to change the simulated supply voltage (replies
`SIM:VIN:4200:OK`).

## States and the LED

| State | LED | Meaning |
| --- | --- | --- |
| SAFE | 1 s on, 1 s off | Idle; Start is allowed |
| ARMING | blinks 2× per second | Safety timer running (5 min, 10 s in the test build) |
| ARMED | blinks 5× per second | Deploy is allowed |
| ACTUATED | solid for 3 s, then off | Deployed; stays here until Stop |
| FAULT | three short flashes, pause | Something failed; Stop retries the self-test |

The deployment is written to the journal on flash before the pulse starts, as
`UPTIME_MS:10000,STATE:ACTUATED,COUNT:1`. If that write fails, there is no pulse
and the board faults with ERR 5.

## Error codes

| ERR | Meaning | Usual cause and fix |
| --- | --- | --- |
| 0 | None | |
| 1 | Self-test failure | UART2, Wi-Fi, watchdog, chip or heap check; see the `POST:` line |
| 2 | Invalid command | Unknown or malformed line on UART/USB; send STOP |
| 3 | Control timeout | No command, status request or PWM for 30 s in ARMING or ARMED; send STOP |
| 4 | UART line overflow | Line longer than 63 characters; send STOP |
| 5 | Storage failure | Filesystem not flashed or damaged; reflash the filesystem image |
| 6 | Settings invalid | `settings.json` missing or wrong; the log names the field, fix it and reflash |
| 7 | Button stuck | BOOT held during start-up; release it and send STOP |
| 8 | PWM signal invalid | Pulses out of range in ARMING or ARMED, often a loose wire picking up noise |
| 9 | PWM signal lost | No pulses in ARMING or ARMED; check the M1 wire |
| 10 | Supply out of range | Voltage outside 4.5–5.5 V for 1 s; restore it, then STOP |
| 11 | Sensor failure | Voltage sensor unreadable; STOP after it recovers |

STOP clears a fault only when the self-test passes. Settings are read once at boot,
so ERR 6 needs a corrected file and a restart.

## Settings

`data/settings.json` holds the PWM pin and bands, the button pin, timings and the
supply limits. The firmware rejects the whole file if any field is missing, has
the wrong type or makes no sense: bands that overlap, a pin used by UART or the
LED, and so on. Set `"button": -1` to disable the button. After editing, flash the
filesystem image again (this wipes the journal).

The supply voltage is simulated for now, at 5.0 V by default. A real reading needs
a voltage divider into an ADC pin, which the sensor interface is ready for.

## Testing

### Without hardware

```sh
python3 scripts/test_native.py      # firmware logic, 89 C++ tests with sanitizers
python3 scripts/test_simulator.py   # scripted run through the terminal simulator
node --test test/web/panel.test.cjs # the web page's JavaScript
```

`test_native.py` compiles the real firmware sources against fake hardware and
runs them with a fake clock, so five minutes of arming takes a fraction of a
second. It covers the state machine, PWM decoding, settings validation, the
self-test, the supply and button monitors, UART parsing and the HTTP API.

To try the state machine by hand, run the terminal simulator and type commands.
`TICK:<ms>` moves time forward, `FAULT` injects an error, and `--normal` uses the
five-minute timer:

```sh
python3 scripts/simulator.py
```

```text
CMD:START
TICK:10000
CMD:DEPLOY
TICK:3000
CMD:STOP
```

### On the bench

These need the test build on the ESP32. The flight-controller scripts refuse to
run if the flight controller is armed. Close the serial monitor first, since the
scripts need the port. Install pymavlink once:

```sh
~/.platformio/penv/bin/pip install --target .pio/python-deps pymavlink
```

USB commands, then the same through ArduPilot and UART6. macOS may rename the
flight controller's port when you move its cable, so check
`~/.platformio/penv/bin/pio device list` if `/dev/cu.usbmodem2101` is not found:

```sh
~/.platformio/penv/bin/python scripts/hardware_smoke.py --port /dev/cu.usbserial-A5069RR4
~/.platformio/penv/bin/python scripts/hardware_smoke.py --via-fc --port /dev/cu.usbmodem2101
```

Both check that DEPLOY is refused early, then arm, deploy, stop, send a bad line and
recover. They end in SAFE.

PWM only, about a minute:

```sh
~/.platformio/penv/bin/python scripts/pwm_bench.py
```

It moves M1 through STOP, START and DEPLOY and only listens to the ESP32. It holds
ARMED for 35 seconds without sending anything else, which proves the PWM heartbeat,
and checks that an early DEPLOY is refused. M1 is left at 1000 µs.

Things to try by hand with the serial monitor open:

- Lost signal: arm with `set_fc_pwm.py 1500`, wait for ARMED, then pull the wire
  out of GPIO27. Expect `PWM:LOST` and ERR 9. If you pull it at the flight
  controller end instead, the loose wire on GPIO27 picks up noise and you get
  ERR 8. Both are faults, which is the point.
- Supply drop (test build): type `SIM:VIN:4000` in the monitor. One second later
  the board faults with ERR 10. `SIM:VIN:5000`, then STOP from the panel, brings it
  back.
- Button: press BOOT while arming. The log shows `BUTTON:STOP` and the board
  returns to SAFE.

## Flight controller setup (ArduPilot)

The bench used a SpeedyBee F405 V3 on ArduPilot 4.7.1, where R6/T6 are `SERIAL6`
([board docs](https://ardupilot.org/copter/docs/common-speedybeef4-v3.html)). UART6
is set up as a raw port with GPS disabled:

| Parameter | Value |
| --- | --- |
| `SERIAL6_PROTOCOL` | `0` |
| `SERIAL6_BAUD` | `115` (115200 baud) |
| `SERIAL6_OPTIONS` | `0` |
| `GPS1_TYPE` | `0` |
| `GPS2_TYPE` | `0` |

Protocol `0` opens the raw UART without a GPS or MAVLink driver. `-1` would
disable the pins entirely in this version
([SerialManager source](https://raw.githubusercontent.com/ArduPilot/ardupilot/Copter-4.7.1/libraries/AP_SerialManager/AP_SerialManager.cpp)).
Changes take effect after a reboot:

```sh
~/.platformio/penv/bin/python scripts/inspect_fc.py               # read only
~/.platformio/penv/bin/python scripts/configure_fc.py --apply --reboot
```

The original values are saved in `backups/fc-parameters-before.json`. The ESP32's
flash from before this project is in `backups/esp32-before-simulator.bin`.

ArduPilot does not send these ASCII commands by itself, and the F405 V3 build has
no Lua scripting
([feature list](https://firmware.ardupilot.org/Copter/stable/speedybeef4v3/features.txt)).
On the bench, `hardware_smoke.py --via-fc` pushes them through ArduPilot with
MAVLink `SERIAL_CONTROL`. For PWM, M1 needs no configuration while
`SERVO1_FUNCTION` is `0`.

## How the code is laid out

The state machine (`fsm`), PWM decoding (`pwm_decoder`), settings validation
(`settings`), the monitors (`power_monitor`, `button_monitor`) and the self-test
(`self_test`) are plain C++ with no Arduino calls, which is what makes the host
tests possible. Hardware sits in thin adapters: `pwm_input` (edge interrupt on
GPIO27), `device_info` (MAC, chip, flash), `uart_handler`, `web_server`,
`indicators` and `event_log`. `main.cpp` wires them together. Each loop it services
the supply, then PWM, the button, UART and USB, and finally queued HTTP commands.
That order decides which fault gets reported when several happen at once.

The web server runs in its own low-priority task, so a slow client can't stall
the main loop. Pins and timing constants live in `include/config.h`. A build uses
about 48 KB of RAM and 850 KB of flash.
