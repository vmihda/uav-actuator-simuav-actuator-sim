# PWM commands, extended self-test and sensor abstraction

Extends the ESP32 actuator simulator
([2026-10-02 design](2026-10-02-actuator-sim-design.md)) to the revised
assignment. The actuator output remains a logical, timed flag; no power GPIO.

## Requirement coverage

| Assignment item | Status before | This design |
| --- | --- | --- |
| Initialization and maximum check: filesystem, ESP ID, button, settings, components | Filesystem and components only | Self-test with settings, chip identity, heap, button, power sensor and PWM capture |
| Safe, Arm and simulated actuation | Done | Unchanged |
| PWM command handling | Missing | PWM input on GPIO27 with a band decoder |
| Web page with Start, Stop and actuation | Done | Adds names for error codes 6–11 only |
| Start begins a five-minute safety timer, then Arm | Done | Unchanged (build flag; 10 s in `esp32dev-test`) |
| In Arm, Stop returns to Safe; actuation starts the safe simulation | Done | Unchanged |
| Sensor abstraction even without physical sensors | Missing | `Sensor` interface with a simulated supply-voltage sensor |
| LED or buzzer state indication | Done | Unchanged |
| Fail-safe on invalid data or errors | Done for UART/HTTP/storage | Adds settings, button, PWM and power faults |
| Optional: physical button | Missing | BOOT button (GPIO0) as local STOP |
| Optional: supply-voltage monitoring | Missing | Power monitor over the simulated sensor |

Out of scope: web diagnostics panel, web control of the simulated voltage, a
real ADC sensor, command-source tracking and changes to fault-code latching.
`ActuatorState`, the UART telemetry line, LED patterns and the arming delay build
flag do not change.

## Hardware

- PWM: SpeedyBee F405 V3 output **M1** → ESP32 **GPIO27**. M1 is the third
  contact of the 8-pin ESC connector counting from `G` (`G V 1 2 3 4 C T`).
  `V` is never connected. The flight controller runs from USB only, without a
  battery. Ground is shared through the existing UART6 wiring.
- Verified on 2026-10-03 before implementation: `SERVO1_FUNCTION=0`,
  `MOT_PWM_TYPE=0`, `BRD_SAFETY_DEFLT=0`. MAVLink `DO_SET_SERVO` on servo 1 was
  accepted and reported back by `SERVO_OUTPUT_RAW`. A multimeter on M1 read
  0.166 V at 1000 µs and 0.33 V at 2000 µs, i.e. a 50 Hz, 3.3 V pulse train.
- Button: on-board BOOT (GPIO0), active low with the internal pull-up. GPIO0 is
  a strapping pin: a button held from power-on keeps the chip in the ROM
  bootloader, so the application never starts and no output can occur.
- Supply voltage: simulated. A divider from VIN to an ADC pin is future work.

## Command channels

| Channel | Role |
| --- | --- |
| PWM (M1 → GPIO27) | Primary command channel from the flight controller |
| UART2 (UART6 on the FC) | Status telemetry and bench text commands, unchanged |
| HTTP | Manual Start/Stop/Deploy, unchanged |
| BOOT button | Local STOP |
| USB (test build only) | Existing bench commands plus `SIM:VIN:<mV>` |

All channels feed the same `dispatchCommand`. The FSM decides acceptance by
state, so STOP wins without a separate priority mechanism.

## Settings

`/settings.json` lives in LittleFS next to `/info.txt` and is installed by the
filesystem image from `data/`. It is parsed with ArduinoJson 7.4.3 (single header
vendored in `lib/ArduinoJson`) and validated strictly at boot.

```json
{
  "version": 1,
  "pins": {"pwm_input": 27, "button": 0},
  "pwm": {
    "valid_min_us": 800, "valid_max_us": 2200,
    "stop": {"min_us": 900, "max_us": 1300},
    "start": {"min_us": 1400, "max_us": 1600},
    "deploy": {"min_us": 1700, "max_us": 2100},
    "stable_ms": 200, "loss_timeout_ms": 500
  },
  "power": {"min_mv": 4500, "max_mv": 5500, "simulated_default_mv": 5000, "stable_ms": 1000},
  "button": {"debounce_ms": 50, "stuck_ms": 500}
}
```

Validation (any failure → FAULT `SettingsInvalid`):

- The file exists, is valid JSON, has `version` 1, every field above and no
  unknown keys. A missing or partial file is never completed from defaults.
- `pins.pwm_input` is an existing GPIO (0–39 excluding 6–11, 20, 24, 28–31).
  `pins.button` is `-1` (disabled) or such a GPIO outside 34–39, which have no
  pull-up. Neither may equal the UART pins (16, 17), the LED pin (2) or each other.
- PWM: `valid_min_us < stop.min_us ≤ stop.max_us < start.min_us ≤ start.max_us
  < deploy.min_us ≤ deploy.max_us ≤ valid_max_us`. Strict inequalities between
  bands keep the dead zones non-empty. `50 ≤ stable_ms < loss_timeout_ms ≤ 5000`.
- Power: `3000 ≤ min_mv < max_mv ≤ 30000`; `simulated_default_mv` lies within
  `[min_mv, max_mv]`; `100 ≤ stable_ms ≤ 10000`.
- Button: `10 ≤ debounce_ms ≤ 200`; `100 ≤ stuck_ms ≤ 5000`.

Each failure is logged with the field and reason, for example
`POST:SETTINGS:FAIL:pwm.start.min_us must exceed pwm.stop.max_us`. JSON syntax
errors include the ArduinoJson error kind (it reports no offset). Settings load once at boot;
recovery cannot clear `SettingsInvalid`, which requires a corrected image and a
reboot.

Deployment: the current board needs one filesystem upload to receive
`settings.json`. This erases the existing deployment journal (bench records
only). Later updates remain firmware-only.

## PWM decoding

`pwm_input` measures the high time of each pulse on the configured pin with an
edge interrupt and `esp_timer_get_time()`. The ISR stores `{widthUs, seq}` under
a spinlock; the main loop copies it under the same spinlock and passes a new
pulse to `PwmCommandDecoder`. Missing pulses between loop iterations are
irrelevant because only the latest width matters.

`PwmCommandDecoder` is pure logic: `onPulse(widthUs, nowMs)`, `update(nowMs)`
returning an event, and `onFault()`. Bands are inclusive ranges from settings;
everything between command bands is a dead zone.

1. **Capture.** Pulses inside `[valid_min_us, valid_max_us]` for `stable_ms`
   capture the signal. Isolated out-of-range pulses are ignored.
2. **First band is silent.** The first stable band after capture records the
   last commanded band without emitting a command. Neutral is established if
   that band is STOP. A controller already at STOP therefore never triggers a
   recovery attempt merely by appearing.
3. **Commands.** A command is emitted when the width stays in a command band
   for `stable_ms` and that band differs from the last commanded band. Dead
   zones never change the last commanded band, so jitter cannot repeat a command.
4. **Neutral.** START and DEPLOY are emitted only after neutral; STOP always is
   and establishes neutral. A premature DEPLOY in ARMING is emitted, rejected by
   the FSM and consumed: the system later reaches ARMED without actuating until
   the width leaves the DEPLOY band and returns after neutral.
5. **Heartbeat.** Each valid pulse while captured and neutral refreshes the FSM
   control deadline (`ActuatorFsm::heartbeat(now)`), so a controller holding
   START keeps a five-minute arming alive.
6. **Loss and invalid signal.** No pulse for `loss_timeout_ms` emits `Lost`;
   widths outside the valid range for `loss_timeout_ms` emit `Invalid`. Either
   releases capture, clears neutral and forgets the last commanded band. The
   main loop turns them into FAULT `PwmLost` / `PwmInvalid` only if the signal
   was captured and the FSM is ARMING or ARMED; otherwise they are logged only.
7. **FAULT.** When the FSM enters FAULT, `onFault()` keeps neutral only if the
   last commanded band is STOP. A controller still holding START or DEPLOY
   cannot command after recovery until it passes through STOP.

## Sensors and power monitoring

```cpp
struct SensorReading { bool valid; int32_t value; };
class Sensor {
 public:
  virtual ~Sensor() = default;
  virtual const char* name() const = 0;
  virtual bool isSimulated() const = 0;
  virtual SensorReading read(uint32_t nowMs) = 0;
};
```

`SimulatedSensor` returns a settable value (initially
`power.simulated_default_mv`) and supports an injected invalid reading for
tests. `PowerMonitor` reads the sensor every loop and reports `Ok`,
`OutOfRange` (outside `[min_mv, max_mv]` continuously for `stable_ms`) or
`SensorFailure` (invalid readings continuously for `stable_ms`). There is no
recovery hysteresis because FAULT is left only through STOP.

Monitoring runs in every state. In SAFE, ARMING, ARMED or ACTUATED a bad verdict
enters FAULT `PowerOutOfRange` or `SensorFailure`; in ACTUATED this ends the
pulse. In FAULT the verdict is only logged.

The test build accepts `SIM:VIN:<mV>` over USB only (integer 0–30000). It replies
`SIM:VIN:<mV>:OK`, or `SIM:VIN:<mV>:REJECTED` when the sensor is not simulated.
The normal build's parser has no such command. The simulated value survives
FAULT recovery and resets to the default on reboot. It is not a heartbeat.

## Button

`ButtonMonitor` debounces the raw level with `debounce_ms` and reports a press
once per debounced press. A press is a STOP command from the button source.
It reports *stuck* when the button is held continuously for `stuck_ms`.

## Self-test

Boot runs the checks in order and reports the first failure's code. Later
checks still run and log, except those that need settings (`BUTTON`, `POWER`,
`PWM`), which log `POST:<CHECK>:SKIPPED` when settings failed. Every check logs
`POST:<CHECK>:OK`, `POST:<CHECK>:FAIL:<detail>` or `SKIPPED` over USB.

| # | Check | Detail | Code on failure |
| --- | --- | --- | --- |
| 1 | `FS` | Existing journal and marker checks | 5 `StorageFailure` |
| 2 | `SETTINGS` | Load and validate `/settings.json` | 6 `SettingsInvalid` |
| 3 | `CHIP` | Factory MAC from eFuse (CRC verified, not all zero), chip model ESP32, detected flash ≥ 4 MB | 1 `SelfTestFailed` |
| 4 | `HEAP` | Free heap ≥ `config::kMinFreeHeapBytes` (64 KiB) | 1 `SelfTestFailed` |
| 5 | `BUTTON` | Not held continuously for `stuck_ms` during the boot window; skipped if disabled | 7 `ButtonStuck` |
| 6 | `POWER` | Power monitor verdict `Ok` after the boot window | 11 `SensorFailure` or 10 `PowerOutOfRange` |
| 7 | `PWM` | Edge interrupt registered on the configured pin; no signal is not an error | 1 `SelfTestFailed` |
| 8 | `LINKS` | UART2, SoftAP and task watchdog, as today | 1 `SelfTestFailed` |

The boot window samples button and power together for
`max(button.stuck_ms, power.stable_ms)` (1 s by default), well within the 5 s
watchdog. A device line `DEVICE:MAC=<mac>,CHIP=<model>,REV=<n>,FLASH_MB=<n>` is
logged at boot.

Recovery (STOP in FAULT) replaces today's `healthCheck()` with the same checks
except `CHIP` and settings parsing. It reuses the boot-time settings result and
the running monitors' current verdicts instead of waiting, so a STOP from HTTP
answers within the one-second command deadline. A button press that triggers
recovery is not "stuck" because the check uses the monitor's continuous-hold
state.

## Fault codes

| ERR | Meaning |
| --- | --- |
| 0–5 | Unchanged |
| 6 | `SettingsInvalid` |
| 7 | `ButtonStuck` |
| 8 | `PwmInvalid` |
| 9 | `PwmLost` |
| 10 | `PowerOutOfRange` |
| 11 | `SensorFailure` |

## Main loop order

The order is a contract because it decides which fault is reported when
several occur in one iteration:

1. Power monitor
2. PWM decoder (pulse, events, heartbeat)
3. Button
4. UART2 and the USB console
5. Queued HTTP command
6. FSM update, indication, status publication

USB diagnostics add a line when the PWM band, capture or neutral state changes
(`PWM:<width>us,BAND:<name>,CAPTURED:<0|1>,NEUTRAL:<0|1>`), one per emitted PWM
command (`PWM:CMD:<name>`), and a supply line when the power verdict changes.

## Modules

| Module | Pure (host-tested) | Purpose |
| --- | --- | --- |
| `settings` | yes | Settings struct, JSON parsing and validation with messages |
| `pwm_decoder` | yes | Rules 1–7 above |
| `pwm_input` | no | Edge ISR and spinlock hand-off |
| `sensor`, `simulated_sensor` | yes | Sensor interface and simulated implementation |
| `power_monitor` | yes | Range and persistence verdict |
| `button_monitor` | yes | Debounce, press events and stuck detection |
| `self_test` | yes, with injected checks | Boot and recovery sequences, log lines |
| `device_info` | no | MAC, chip model and revision, flash size, free heap |

`ActuatorFsm` gains `heartbeat(now)`. `ErrorCode` gains values 6–11. The UART
parser gains `SIM:VIN:<mV>` only when constructed for the test-build USB
console.

## Testing

- Host (TDD, ASan/UBSan, strict warnings): settings parsing and every validation
  rule; decoder rules 1–7 including the silent first band, consumed premature
  DEPLOY, jitter at band edges, heartbeat and neutral after FAULT; power monitor
  persistence; button debounce and stuck; self-test boot and recovery with fake
  checks; FSM heartbeat; `SIM:VIN` parsing only in the test console.
- Panel: names for error codes 6–11.
- Hardware: `scripts/pwm_bench.py` drives M1 through `DO_SET_SERVO` and reads
  ESP32 telemetry over USB. Sequence: 1000 (capture and neutral) → 1500
  (ARMING) → ARMED → hold for 35 s with PWM only (heartbeat) → 2000 (ACTUATED) →
  1000 (SAFE) → 1500 then 2000 at once (DEPLOY rejected in ARMING) → ARMED
  without actuating → 1000 (SAFE). It refuses to run while the controller is
  armed and leaves M1 at 1000. Pulling the GPIO27 jumper while ARMING is a
  manual check for `PwmLost`. The existing `hardware_smoke.py` still passes.
