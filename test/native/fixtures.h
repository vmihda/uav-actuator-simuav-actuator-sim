#pragma once

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "settings.h"

inline std::string readText(const char* path) {
  std::ifstream file(path);
  if (!file) throw std::runtime_error(std::string("cannot read ") + path);
  std::ostringstream text;
  text << file.rdbuf();
  return text.str();
}

inline std::string replaced(std::string text, const std::string& from, const std::string& to) {
  const auto at = text.find(from);
  if (at == std::string::npos) throw std::runtime_error("fixture text not found: " + from);
  return text.replace(at, from.size(), to);
}

// The shipped data/settings.json, parsed; tests run from the repository root.
inline Settings defaultSettings() {
  const std::string json = readText("data/settings.json");
  Settings settings{};
  char error[128];
  if (!parseSettings(json.c_str(), json.size(), settings, error, sizeof(error)))
    throw std::runtime_error(std::string("data/settings.json: ") + error);
  return settings;
}
