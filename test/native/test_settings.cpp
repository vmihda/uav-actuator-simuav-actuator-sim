#include <LittleFS.h>
#include <cstring>
#include <string>

#include "fixtures.h"
#include "harness.h"
#include "settings.h"
#include "settings_file.h"

namespace {
std::string rejection(const std::string& json) {
  Settings settings{};
  char error[128];
  if (parseSettings(json.c_str(), json.size(), settings, error, sizeof(error)))
    throw std::runtime_error("settings unexpectedly accepted");
  return error;
}

std::string defaults() { return readText("data/settings.json"); }
}  // namespace

void runSettingsTests() {
  test("shipped settings parse with the specified defaults", [] {
    const Settings s = defaultSettings();
    CHECK(s.pwmInputPin == 27 && s.buttonPin == 0);
    CHECK(s.pwmValidMinUs == 800 && s.pwmValidMaxUs == 2200);
    CHECK(s.stop.minUs == 900 && s.stop.maxUs == 1300);
    CHECK(s.start.minUs == 1400 && s.start.maxUs == 1600);
    CHECK(s.deploy.minUs == 1700 && s.deploy.maxUs == 2100);
    CHECK(s.pwmStableMs == 200 && s.pwmLossTimeoutMs == 500);
    CHECK(s.powerMinMv == 4500 && s.powerMaxMv == 5500);
    CHECK(s.powerSimulatedDefaultMv == 5000 && s.powerStableMs == 1000);
    CHECK(s.buttonDebounceMs == 50 && s.buttonStuckMs == 500);
    CHECK(s.contactPin == 26);
    CHECK(s.contactDebounceMs == 20 && s.contactStuckMs == 2000);
    CHECK(s.contactArmedTimeoutMs == 120000);
  });
  test("missing, unknown and malformed settings name the field", [] {
    CHECK(rejection(replaced(defaults(), "\"stable_ms\": 200, ", "")) ==
          "pwm.stable_ms is missing");
    CHECK(rejection(replaced(defaults(), "\"version\": 2,", "\"version\": 2, \"extra\": 1,")) ==
          "extra is not a known setting");
    CHECK(rejection(replaced(defaults(), "\"contact\": 26}", "\"contact\": 26, \"led\": 4}")) ==
          "pins.led is not a known setting");
    CHECK(rejection(replaced(defaults(), "\"version\": 2", "\"version\": 1")) ==
          "version must be within 2..2");
    CHECK(rejection(replaced(defaults(), "\"pins\": {\"pwm_input\": 27, \"button\": 0, \"contact\": 26}", "\"pins\": 5")) ==
          "pins must be an object");
    CHECK(rejection("{\"version\": 2,").find("settings.json is not valid JSON") == 0);
    CHECK(rejection("[1, 2]") == "settings.json must be an object");
  });
  test("settings reject values of the wrong JSON type", [] {
    CHECK(rejection(replaced(defaults(), "\"pwm_input\": 27", "\"pwm_input\": \"27\"")) ==
          "pins.pwm_input must be an integer");
    CHECK(rejection(replaced(defaults(), "\"pwm_input\": 27", "\"pwm_input\": 27.5")) ==
          "pins.pwm_input must be an integer");
    CHECK(rejection(replaced(defaults(), "\"stuck_ms\": 500", "\"stuck_ms\": true")) ==
          "button.stuck_ms must be an integer");
  });
  test("PWM bands must be ordered with non-empty dead zones", [] {
    CHECK(rejection(replaced(defaults(), "\"min_us\": 900", "\"min_us\": 800")) ==
          "pwm.stop.min_us must exceed pwm.valid_min_us");
    CHECK(rejection(replaced(defaults(), "\"min_us\": 1400", "\"min_us\": 1300")) ==
          "pwm.start.min_us must exceed pwm.stop.max_us");
    CHECK(rejection(replaced(defaults(), "\"min_us\": 1700", "\"min_us\": 1600")) ==
          "pwm.deploy.min_us must exceed pwm.start.max_us");
    CHECK(rejection(replaced(defaults(), "\"max_us\": 2100", "\"max_us\": 2300")) ==
          "pwm.deploy.max_us must not exceed pwm.valid_max_us");
    CHECK(rejection(replaced(defaults(), "\"max_us\": 1600", "\"max_us\": 1350")) ==
          "pwm.start.max_us must not be below pwm.start.min_us");
    CHECK(rejection(replaced(defaults(), "\"stable_ms\": 200", "\"stable_ms\": 500")) ==
          "pwm.stable_ms must be below pwm.loss_timeout_ms");
    CHECK(rejection(replaced(defaults(), "\"stable_ms\": 200", "\"stable_ms\": 40")) ==
          "pwm.stable_ms must be within 50..5000");
  });
  test("pins must be usable and must not collide", [] {
    CHECK(rejection(replaced(defaults(), "\"pwm_input\": 27", "\"pwm_input\": 16")) ==
          "pins.pwm_input conflicts with the UART or LED pins");
    CHECK(rejection(replaced(defaults(), "\"pwm_input\": 27", "\"pwm_input\": 7")) ==
          "pins.pwm_input is not a usable GPIO");
    CHECK(rejection(replaced(defaults(), "\"button\": 0,", "\"button\": 35,")) ==
          "pins.button needs an internal pull-up (GPIO 0-33)");
    CHECK(rejection(replaced(defaults(), "\"button\": 0,", "\"button\": 27,")) ==
          "pins.button must differ from pins.pwm_input");
    CHECK(rejection(replaced(defaults(), "\"button\": 0,", "\"button\": 2,")) ==
          "pins.button conflicts with the UART or LED pins");
    const std::string disabled = replaced(defaults(), "\"button\": 0,", "\"button\": -1,");
    Settings settings{};
    char error[128];
    CHECK(parseSettings(disabled.c_str(), disabled.size(), settings, error, sizeof(error)));
    CHECK(settings.buttonPin == -1);
  });
  test("power and button settings are range checked", [] {
    CHECK(rejection(replaced(defaults(), "\"max_mv\": 5500", "\"max_mv\": 4500")) ==
          "power.max_mv must exceed power.min_mv");
    CHECK(rejection(replaced(defaults(), "\"simulated_default_mv\": 5000", "\"simulated_default_mv\": 6000")) ==
          "power.simulated_default_mv must lie within power.min_mv..power.max_mv");
    CHECK(rejection(replaced(defaults(), "\"min_mv\": 4500", "\"min_mv\": 2000")) ==
          "power.min_mv must be within 3000..30000");
    CHECK(rejection(replaced(defaults(), "\"debounce_ms\": 50", "\"debounce_ms\": 5")) ==
          "button.debounce_ms must be within 10..200");
  });
  test("the contact whisker pin must be usable and must not collide", [] {
    CHECK(rejection(replaced(defaults(), "\"contact\": 26}", "\"contact\": 27}")) ==
          "pins.contact must differ from pins.pwm_input");
    CHECK(rejection(replaced(defaults(), "\"contact\": 26}", "\"contact\": 0}")) ==
          "pins.contact must differ from pins.button");
    CHECK(rejection(replaced(defaults(), "\"contact\": 26}", "\"contact\": 17}")) ==
          "pins.contact conflicts with the UART or LED pins");
    CHECK(rejection(replaced(defaults(), "\"contact\": 26}", "\"contact\": 36}")) ==
          "pins.contact needs an internal pull-up (GPIO 0-33)");
    CHECK(rejection(replaced(defaults(), "\"contact\": 26}", "\"contact\": 9}")) ==
          "pins.contact is not a usable GPIO");
    const std::string simulated = replaced(defaults(), "\"contact\": 26}", "\"contact\": -1}");
    Settings settings{};
    char error[128];
    CHECK(parseSettings(simulated.c_str(), simulated.size(), settings, error, sizeof(error)));
    CHECK(settings.contactPin == -1);
    // A disabled button frees its pin for the whiskers.
    const std::string shared = replaced(replaced(defaults(), "\"button\": 0,", "\"button\": -1,"),
                                        "\"contact\": 26}", "\"contact\": 0}");
    CHECK(parseSettings(shared.c_str(), shared.size(), settings, error, sizeof(error)));
  });
  test("contact whisker timings are range checked", [] {
    CHECK(rejection(replaced(defaults(), "\"debounce_ms\": 20", "\"debounce_ms\": 2")) ==
          "contact.debounce_ms must be within 5..200");
    CHECK(rejection(replaced(defaults(), "\"stuck_ms\": 2000", "\"stuck_ms\": 4000")) ==
          "contact.stuck_ms must be within 100..3000");
    CHECK(rejection(replaced(replaced(defaults(), "\"debounce_ms\": 20", "\"debounce_ms\": 150"),
                             "\"stuck_ms\": 2000", "\"stuck_ms\": 120")) ==
          "contact.stuck_ms must exceed contact.debounce_ms");
    CHECK(rejection(replaced(defaults(), "\"armed_timeout_ms\": 120000", "\"armed_timeout_ms\": 500")) ==
          "contact.armed_timeout_ms must be within 1000..3600000");
    CHECK(rejection(replaced(defaults(), ", \"armed_timeout_ms\": 120000", "")) ==
          "contact.armed_timeout_ms is missing");
  });
  test("a small error buffer is truncated, never overflowed", [] {
    const std::string json = replaced(defaults(), "\"version\": 2", "\"version\": 1");
    Settings settings{};
    char error[8];
    std::memset(error, 'X', sizeof(error));
    CHECK(!parseSettings(json.c_str(), json.size(), settings, error, sizeof(error)));
    CHECK(std::strcmp(error, "version") == 0);
  });
  test("settings file loader reports a missing file and loads a valid one", [] {
    LittleFS = LittleFSClass();
    Settings settings{};
    char error[128];
    CHECK(!loadSettingsFile(settings, error, sizeof(error)));
    CHECK(std::strcmp(error, "settings.json is missing") == 0);
    LittleFS.files["/settings.json"] = readText("data/settings.json");
    CHECK(loadSettingsFile(settings, error, sizeof(error)));
    CHECK(settings.pwmInputPin == 27);
    LittleFS.files["/settings.json"] = std::string(4096, ' ');
    CHECK(!loadSettingsFile(settings, error, sizeof(error)));
    CHECK(std::strcmp(error, "settings.json size must be 1..2048 bytes") == 0);
  });
}
