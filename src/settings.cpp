#include "settings.h"

#include <ArduinoJson.h>
#include <cstdio>
#include <cstring>

#include "config.h"

namespace {
class Errors {
 public:
  Errors(char* buffer, std::size_t capacity) : buffer_(buffer), capacity_(capacity) {
    if (capacity_ > 0) buffer_[0] = '\0';
  }
  // Records only the first failure; always returns false for use in returns.
  bool fail(const char* field, const char* reason) {
    if (!failed_ && capacity_ > 0) std::snprintf(buffer_, capacity_, "%s %s", field, reason);
    failed_ = true;
    return false;
  }

 private:
  char* buffer_;
  std::size_t capacity_;
  bool failed_ = false;
};

bool onlyKeys(JsonObjectConst object, const char* path, const char* const* keys,
              std::size_t count, Errors& errors) {
  for (JsonPairConst pair : object) {
    bool known = false;
    for (std::size_t i = 0; i < count; ++i) known = known || std::strcmp(pair.key().c_str(), keys[i]) == 0;
    if (known) continue;
    char field[64];
    if (path[0] == '\0') std::snprintf(field, sizeof(field), "%s", pair.key().c_str());
    else std::snprintf(field, sizeof(field), "%s.%s", path, pair.key().c_str());
    return errors.fail(field, "is not a known setting");
  }
  return true;
}

bool readObject(JsonObjectConst parent, const char* key, const char* field,
                JsonObjectConst& out, Errors& errors) {
  const JsonVariantConst value = parent[key];
  if (value.isNull()) return errors.fail(field, "is missing");
  if (!value.is<JsonObjectConst>()) return errors.fail(field, "must be an object");
  out = value.as<JsonObjectConst>();
  return true;
}

bool readInt(JsonObjectConst object, const char* key, const char* field, long min, long max,
             long& out, Errors& errors) {
  const JsonVariantConst value = object[key];
  if (value.isNull()) return errors.fail(field, "is missing");
  // Strings, floats and booleans are rejected rather than coerced.
  if (!value.is<long>()) return errors.fail(field, "must be an integer");
  out = value.as<long>();
  if (out >= min && out <= max) return true;
  char reason[48];
  std::snprintf(reason, sizeof(reason), "must be within %ld..%ld", min, max);
  return errors.fail(field, reason);
}

bool readRange(JsonObjectConst pwm, const char* key, const char* field, PwmRange& out, Errors& errors) {
  JsonObjectConst range;
  if (!readObject(pwm, key, field, range, errors)) return false;
  static const char* const keys[] = {"min_us", "max_us"};
  if (!onlyKeys(range, field, keys, 2, errors)) return false;
  char minField[32];
  char maxField[32];
  std::snprintf(minField, sizeof(minField), "%s.min_us", field);
  std::snprintf(maxField, sizeof(maxField), "%s.max_us", field);
  long min = 0;
  long max = 0;
  if (!readInt(range, "min_us", minField, 500, 2500, min, errors) ||
      !readInt(range, "max_us", maxField, 500, 2500, max, errors)) return false;
  if (max < min) {
    char reason[sizeof("must not be below ") + sizeof(minField)];
    std::snprintf(reason, sizeof(reason), "must not be below %s", minField);
    return errors.fail(maxField, reason);
  }
  out = {static_cast<uint16_t>(min), static_cast<uint16_t>(max)};
  return true;
}

bool isUsableGpio(long pin) {
  if (pin < 0 || pin > 39) return false;
  if (pin >= 6 && pin <= 11) return false;  // Internal SPI flash.
  return pin != 20 && pin != 24 && !(pin >= 28 && pin <= 31);
}

bool isReservedPin(long pin) {
  return pin == config::kUartRxPin || pin == config::kUartTxPin || pin == config::kLedPin;
}

bool readPins(JsonObjectConst pins, Settings& s, Errors& errors) {
  static const char* const keys[] = {"pwm_input", "button", "contact"};
  if (!onlyKeys(pins, "pins", keys, 3, errors)) return false;
  long pin = 0;
  if (!readInt(pins, "pwm_input", "pins.pwm_input", 0, 39, pin, errors)) return false;
  if (!isUsableGpio(pin)) return errors.fail("pins.pwm_input", "is not a usable GPIO");
  if (isReservedPin(pin)) return errors.fail("pins.pwm_input", "conflicts with the UART or LED pins");
  s.pwmInputPin = static_cast<int>(pin);
  if (!readInt(pins, "button", "pins.button", -1, 39, pin, errors)) return false;
  if (pin >= 0) {
    if (!isUsableGpio(pin)) return errors.fail("pins.button", "is not a usable GPIO");
    if (pin >= 34) return errors.fail("pins.button", "needs an internal pull-up (GPIO 0-33)");
    if (isReservedPin(pin)) return errors.fail("pins.button", "conflicts with the UART or LED pins");
    if (pin == s.pwmInputPin) return errors.fail("pins.button", "must differ from pins.pwm_input");
  }
  s.buttonPin = static_cast<int>(pin);
  if (!readInt(pins, "contact", "pins.contact", -1, 39, pin, errors)) return false;
  if (pin >= 0) {
    if (!isUsableGpio(pin)) return errors.fail("pins.contact", "is not a usable GPIO");
    if (pin >= 34) return errors.fail("pins.contact", "needs an internal pull-up (GPIO 0-33)");
    if (isReservedPin(pin)) return errors.fail("pins.contact", "conflicts with the UART or LED pins");
    if (pin == s.pwmInputPin) return errors.fail("pins.contact", "must differ from pins.pwm_input");
    if (pin == s.buttonPin) return errors.fail("pins.contact", "must differ from pins.button");
  }
  s.contactPin = static_cast<int>(pin);
  return true;
}

bool readPwm(JsonObjectConst pwm, Settings& s, Errors& errors) {
  static const char* const keys[] = {"valid_min_us", "valid_max_us", "stop", "start", "deploy",
                                     "stable_ms", "loss_timeout_ms"};
  if (!onlyKeys(pwm, "pwm", keys, 7, errors)) return false;
  long validMin = 0;
  long validMax = 0;
  if (!readInt(pwm, "valid_min_us", "pwm.valid_min_us", 500, 2500, validMin, errors) ||
      !readInt(pwm, "valid_max_us", "pwm.valid_max_us", 500, 2500, validMax, errors) ||
      !readRange(pwm, "stop", "pwm.stop", s.stop, errors) ||
      !readRange(pwm, "start", "pwm.start", s.start, errors) ||
      !readRange(pwm, "deploy", "pwm.deploy", s.deploy, errors)) return false;
  s.pwmValidMinUs = static_cast<uint16_t>(validMin);
  s.pwmValidMaxUs = static_cast<uint16_t>(validMax);
  // Strict inequalities keep a dead zone between neighbouring bands.
  if (s.stop.minUs <= s.pwmValidMinUs)
    return errors.fail("pwm.stop.min_us", "must exceed pwm.valid_min_us");
  if (s.start.minUs <= s.stop.maxUs)
    return errors.fail("pwm.start.min_us", "must exceed pwm.stop.max_us");
  if (s.deploy.minUs <= s.start.maxUs)
    return errors.fail("pwm.deploy.min_us", "must exceed pwm.start.max_us");
  if (s.deploy.maxUs > s.pwmValidMaxUs)
    return errors.fail("pwm.deploy.max_us", "must not exceed pwm.valid_max_us");
  long stable = 0;
  long loss = 0;
  if (!readInt(pwm, "stable_ms", "pwm.stable_ms", 50, 5000, stable, errors) ||
      !readInt(pwm, "loss_timeout_ms", "pwm.loss_timeout_ms", 50, 5000, loss, errors)) return false;
  if (stable >= loss) return errors.fail("pwm.stable_ms", "must be below pwm.loss_timeout_ms");
  s.pwmStableMs = static_cast<uint32_t>(stable);
  s.pwmLossTimeoutMs = static_cast<uint32_t>(loss);
  return true;
}

bool readPower(JsonObjectConst power, Settings& s, Errors& errors) {
  static const char* const keys[] = {"min_mv", "max_mv", "simulated_default_mv", "stable_ms"};
  if (!onlyKeys(power, "power", keys, 4, errors)) return false;
  long min = 0;
  long max = 0;
  long fallback = 0;
  long stable = 0;
  if (!readInt(power, "min_mv", "power.min_mv", 3000, 30000, min, errors) ||
      !readInt(power, "max_mv", "power.max_mv", 3000, 30000, max, errors)) return false;
  if (max <= min) return errors.fail("power.max_mv", "must exceed power.min_mv");
  if (!readInt(power, "simulated_default_mv", "power.simulated_default_mv", 3000, 30000, fallback, errors))
    return false;
  if (fallback < min || fallback > max)
    return errors.fail("power.simulated_default_mv", "must lie within power.min_mv..power.max_mv");
  if (!readInt(power, "stable_ms", "power.stable_ms", 100, 10000, stable, errors)) return false;
  s.powerMinMv = static_cast<int32_t>(min);
  s.powerMaxMv = static_cast<int32_t>(max);
  s.powerSimulatedDefaultMv = static_cast<int32_t>(fallback);
  s.powerStableMs = static_cast<uint32_t>(stable);
  return true;
}

bool readButton(JsonObjectConst button, Settings& s, Errors& errors) {
  static const char* const keys[] = {"debounce_ms", "stuck_ms"};
  if (!onlyKeys(button, "button", keys, 2, errors)) return false;
  long debounce = 0;
  long stuck = 0;
  if (!readInt(button, "debounce_ms", "button.debounce_ms", 10, 200, debounce, errors) ||
      !readInt(button, "stuck_ms", "button.stuck_ms", 100, 5000, stuck, errors)) return false;
  s.buttonDebounceMs = static_cast<uint32_t>(debounce);
  s.buttonStuckMs = static_cast<uint32_t>(stuck);
  return true;
}

bool readContact(JsonObjectConst contact, Settings& s, Errors& errors) {
  static const char* const keys[] = {"debounce_ms", "stuck_ms", "armed_timeout_ms"};
  if (!onlyKeys(contact, "contact", keys, 3, errors)) return false;
  long debounce = 0;
  long stuck = 0;
  long armedTimeout = 0;
  // The stuck ceiling keeps the boot sampling window well inside the 5 s watchdog.
  if (!readInt(contact, "debounce_ms", "contact.debounce_ms", 5, 200, debounce, errors) ||
      !readInt(contact, "stuck_ms", "contact.stuck_ms", 100, 3000, stuck, errors)) return false;
  if (stuck <= debounce) return errors.fail("contact.stuck_ms", "must exceed contact.debounce_ms");
  if (!readInt(contact, "armed_timeout_ms", "contact.armed_timeout_ms", 1000, 3600000,
               armedTimeout, errors)) return false;
  s.contactDebounceMs = static_cast<uint32_t>(debounce);
  s.contactStuckMs = static_cast<uint32_t>(stuck);
  s.contactArmedTimeoutMs = static_cast<uint32_t>(armedTimeout);
  return true;
}
}  // namespace

bool parseSettings(const char* json, std::size_t length, Settings& out,
                   char* error, std::size_t capacity) {
  Errors errors(error, capacity);
  JsonDocument document;
  const DeserializationError parsed = deserializeJson(document, json, length);
  if (parsed) {
    char reason[64];
    std::snprintf(reason, sizeof(reason), "is not valid JSON (%s)", parsed.c_str());
    return errors.fail("settings.json", reason);
  }
  if (!document.is<JsonObjectConst>()) return errors.fail("settings.json", "must be an object");
  const JsonObjectConst root = document.as<JsonObjectConst>();
  static const char* const keys[] = {"version", "pins", "pwm", "power", "button", "contact"};
  if (!onlyKeys(root, "", keys, 6, errors)) return false;
  long version = 0;
  JsonObjectConst pins;
  JsonObjectConst pwm;
  JsonObjectConst power;
  JsonObjectConst button;
  JsonObjectConst contact;
  if (!readInt(root, "version", "version", 2, 2, version, errors) ||
      !readObject(root, "pins", "pins", pins, errors) ||
      !readObject(root, "pwm", "pwm", pwm, errors) ||
      !readObject(root, "power", "power", power, errors) ||
      !readObject(root, "button", "button", button, errors) ||
      !readObject(root, "contact", "contact", contact, errors)) return false;
  Settings settings{};
  if (!readPins(pins, settings, errors) || !readPwm(pwm, settings, errors) ||
      !readPower(power, settings, errors) || !readButton(button, settings, errors) ||
      !readContact(contact, settings, errors)) return false;
  out = settings;
  return true;
}
