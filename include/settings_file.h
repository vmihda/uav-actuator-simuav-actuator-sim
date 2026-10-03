#pragma once

#include <cstddef>
#include "settings.h"

// Reads and validates /settings.json from the mounted LittleFS.
bool loadSettingsFile(Settings& out, char* error, std::size_t capacity);
