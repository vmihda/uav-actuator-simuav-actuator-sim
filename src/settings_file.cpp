#include "settings_file.h"

#include <LittleFS.h>
#include <cstdio>

namespace {
constexpr std::size_t kMaxSettingsBytes = 2048;
char buffer[kMaxSettingsBytes];

bool fail(char* error, std::size_t capacity, const char* message) {
  if (capacity > 0) std::snprintf(error, capacity, "%s", message);
  return false;
}
}  // namespace

bool loadSettingsFile(Settings& out, char* error, std::size_t capacity) {
  if (!LittleFS.exists("/settings.json")) return fail(error, capacity, "settings.json is missing");
  File file = LittleFS.open("/settings.json", FILE_READ);
  if (!file || file.isDirectory()) return fail(error, capacity, "settings.json is unreadable");
  const std::size_t size = file.size();
  if (size == 0 || size > kMaxSettingsBytes) {
    file.close();
    return fail(error, capacity, "settings.json size must be 1..2048 bytes");
  }
  const std::size_t read = file.readBytes(buffer, size);
  file.close();
  if (read != size) return fail(error, capacity, "settings.json is unreadable");
  return parseSettings(buffer, size, out, error, capacity);
}
