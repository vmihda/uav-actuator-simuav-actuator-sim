#include <string>
#include <vector>

#include "harness.h"
#include "self_test.h"

namespace {
std::vector<std::string> lines;
void capture(const char* line) { lines.push_back(line); }

SelfTestInputs healthy() {
  SelfTestInputs in{};
  in.storageOk = true;
  in.settingsOk = true;
  in.settingsError = "";
  in.device = DeviceInfo{true, {0x24, 0x6F, 0x28, 0x01, 0x02, 0x03}, true, "ESP32-D0WD-V3", 3,
                         4u * 1024 * 1024, 200000};
  in.buttonEnabled = true;
  in.buttonStuck = false;
  in.powerVerdict = PowerVerdict::Ok;
  in.powerInstant = PowerVerdict::Ok;
  in.pwmReady = true;
  in.linksOk = true;
  return in;
}

ErrorCode run(SelfTestMode mode, const SelfTestInputs& in) {
  lines.clear();
  return runSelfTest(mode, in, capture);
}
}  // namespace

void runSelfTestTests() {
  test("a healthy boot self-test logs all eight checks", [] {
    CHECK(run(SelfTestMode::Boot, healthy()) == ErrorCode::None);
    const std::vector<std::string> expected = {"POST:FS:OK", "POST:SETTINGS:OK", "POST:CHIP:OK",
        "POST:HEAP:OK", "POST:BUTTON:OK", "POST:POWER:OK", "POST:PWM:OK", "POST:LINKS:OK"};
    CHECK(lines == expected);
  });
  test("the first failing check decides the fault code", [] {
    SelfTestInputs in = healthy();
    in.settingsOk = false;
    in.settingsError = "pwm.stable_ms is missing";
    in.device.freeHeap = 1000;
    CHECK(run(SelfTestMode::Boot, in) == ErrorCode::SettingsInvalid);
    CHECK(lines[1] == "POST:SETTINGS:FAIL:pwm.stable_ms is missing");
    CHECK(lines[3] == "POST:HEAP:FAIL:free heap below minimum");
  });
  test("checks that need settings are skipped when settings fail", [] {
    SelfTestInputs in = healthy();
    in.settingsOk = false;
    in.buttonStuck = true;
    in.powerVerdict = PowerVerdict::OutOfRange;
    CHECK(run(SelfTestMode::Boot, in) == ErrorCode::SettingsInvalid);
    CHECK(lines[4] == "POST:BUTTON:SKIPPED");
    CHECK(lines[5] == "POST:POWER:SKIPPED");
    CHECK(lines[6] == "POST:PWM:SKIPPED");
  });
  test("each failing check maps to its fault code", [] {
    SelfTestInputs in = healthy();
    in.storageOk = false;
    CHECK(run(SelfTestMode::Boot, in) == ErrorCode::StorageFailure);
    in = healthy();
    in.buttonStuck = true;
    CHECK(run(SelfTestMode::Boot, in) == ErrorCode::ButtonStuck);
    in = healthy();
    in.powerVerdict = PowerVerdict::OutOfRange;
    CHECK(run(SelfTestMode::Boot, in) == ErrorCode::PowerOutOfRange);
    in = healthy();
    in.powerVerdict = PowerVerdict::SensorFailure;
    CHECK(run(SelfTestMode::Boot, in) == ErrorCode::SensorFailure);
    in = healthy();
    in.pwmReady = false;
    CHECK(run(SelfTestMode::Boot, in) == ErrorCode::SelfTestFailed);
    in = healthy();
    in.linksOk = false;
    CHECK(run(SelfTestMode::Boot, in) == ErrorCode::SelfTestFailed);
  });
  test("a disabled button is skipped, not failed", [] {
    SelfTestInputs in = healthy();
    in.buttonEnabled = false;
    in.buttonStuck = true;
    CHECK(run(SelfTestMode::Boot, in) == ErrorCode::None);
    CHECK(lines[4] == "POST:BUTTON:SKIPPED:disabled");
  });
  test("recovery skips CHIP and rejects a supply that is bad right now", [] {
    SelfTestInputs in = healthy();
    in.device.macValid = false;
    CHECK(run(SelfTestMode::Recovery, in) == ErrorCode::None);
    CHECK(lines[2] == "POST:CHIP:SKIPPED");
    in.powerInstant = PowerVerdict::OutOfRange;
    CHECK(run(SelfTestMode::Recovery, in) == ErrorCode::PowerOutOfRange);
  });
  test("chip check rejects an unreadable MAC, another chip and small flash", [] {
    const char* detail = nullptr;
    DeviceInfo device = healthy().device;
    CHECK(chipHealthy(device, detail));
    device.macValid = false;
    CHECK(!chipHealthy(device, detail) && std::string(detail) == "factory MAC unreadable");
    device = healthy().device;
    for (uint8_t& byte : device.mac) byte = 0;
    CHECK(!chipHealthy(device, detail) && std::string(detail) == "factory MAC unreadable");
    device = healthy().device;
    device.isEsp32 = false;
    CHECK(!chipHealthy(device, detail) && std::string(detail) == "unexpected chip model");
    device = healthy().device;
    device.flashBytes = 2u * 1024 * 1024;
    CHECK(!chipHealthy(device, detail) && std::string(detail) == "flash smaller than 4 MB");
    device = healthy().device;
    device.flashBytes = 8u * 1024 * 1024;
    CHECK(chipHealthy(device, detail));
  });
}
