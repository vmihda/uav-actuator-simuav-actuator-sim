#include "self_test.h"

#include <cstdio>

#include "config.h"

namespace {
class Report {
 public:
  explicit Report(SelfTestLog log) : log_(log) {}
  void ok(const char* check) { line(check, "OK", nullptr); }
  void skipped(const char* check, const char* detail = nullptr) { line(check, "SKIPPED", detail); }
  void fail(const char* check, const char* detail, ErrorCode code) {
    line(check, "FAIL", detail);
    if (first_ == ErrorCode::None) first_ = code;
  }
  ErrorCode first() const { return first_; }

 private:
  void line(const char* check, const char* outcome, const char* detail) {
    char text[160];
    if (detail) std::snprintf(text, sizeof(text), "POST:%s:%s:%s", check, outcome, detail);
    else std::snprintf(text, sizeof(text), "POST:%s:%s", check, outcome);
    log_(text);
  }
  SelfTestLog log_;
  ErrorCode first_ = ErrorCode::None;
};

ErrorCode powerCode(PowerVerdict verdict) {
  return verdict == PowerVerdict::SensorFailure ? ErrorCode::SensorFailure : ErrorCode::PowerOutOfRange;
}
}  // namespace

bool chipHealthy(const DeviceInfo& device, const char*& detail) {
  bool macNonZero = false;
  for (uint8_t byte : device.mac) macNonZero = macNonZero || byte != 0;
  if (!device.macValid || !macNonZero) {
    detail = "factory MAC unreadable";
    return false;
  }
  if (!device.isEsp32) {
    detail = "unexpected chip model";
    return false;
  }
  if (device.flashBytes < config::kMinFlashBytes) {
    detail = "flash smaller than 4 MB";
    return false;
  }
  return true;
}

ErrorCode runSelfTest(SelfTestMode mode, const SelfTestInputs& in, SelfTestLog log) {
  Report report(log);
  if (in.storageOk) report.ok("FS");
  else report.fail("FS", "marker, probe or journal check failed", ErrorCode::StorageFailure);

  if (in.settingsOk) report.ok("SETTINGS");
  else report.fail("SETTINGS", in.settingsError, ErrorCode::SettingsInvalid);

  // Chip identity cannot change after boot, so recovery does not repeat it.
  const char* chipDetail = nullptr;
  if (mode == SelfTestMode::Recovery) report.skipped("CHIP");
  else if (chipHealthy(in.device, chipDetail)) report.ok("CHIP");
  else report.fail("CHIP", chipDetail, ErrorCode::SelfTestFailed);

  if (in.device.freeHeap >= config::kMinFreeHeapBytes) report.ok("HEAP");
  else report.fail("HEAP", "free heap below minimum", ErrorCode::SelfTestFailed);

  if (!in.settingsOk) {
    report.skipped("BUTTON");
    report.skipped("POWER");
    report.skipped("PWM");
  } else {
    if (!in.buttonEnabled) report.skipped("BUTTON", "disabled");
    else if (in.buttonStuck) report.fail("BUTTON", "held down", ErrorCode::ButtonStuck);
    else report.ok("BUTTON");

    // Recovery uses the running monitor: bad now counts even before it persists.
    if (in.powerVerdict != PowerVerdict::Ok)
      report.fail("POWER", powerVerdictName(in.powerVerdict), powerCode(in.powerVerdict));
    else if (in.powerInstant != PowerVerdict::Ok)
      report.fail("POWER", powerVerdictName(in.powerInstant), powerCode(in.powerInstant));
    else report.ok("POWER");

    if (in.pwmReady) report.ok("PWM");
    else report.fail("PWM", "edge interrupt not registered", ErrorCode::SelfTestFailed);
  }

  if (in.linksOk) report.ok("LINKS");
  else report.fail("LINKS", "UART2, SoftAP or watchdog unavailable", ErrorCode::SelfTestFailed);
  return report.first();
}
