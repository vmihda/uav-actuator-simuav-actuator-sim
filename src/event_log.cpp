#include "event_log.h"
#include <LittleFS.h>
#include <cstdio>
#include <cstring>
#include "config.h"

namespace {
bool journalPathsValid() {
  if (LittleFS.exists("/events.previous.log")) {
    File previous = LittleFS.open("/events.previous.log", FILE_READ);
    if (!previous || previous.isDirectory()) return false;
    previous.close();
  }
  if (LittleFS.exists("/events.log")) {
    File existing = LittleFS.open("/events.log", FILE_READ);
    if (!existing || existing.isDirectory()) return false;
    existing.close();
  }
  return true;
}

File prepareJournal(std::size_t incomingBytes) {
  if (!journalPathsValid()) return File();
  if (LittleFS.exists("/events.log")) {
    File existing = LittleFS.open("/events.log", FILE_READ);
    if (!existing) return File();
    const std::size_t currentSize = existing.size();
    existing.close();
    if (currentSize + incomingBytes > config::kJournalMaxBytes) {
      if (LittleFS.exists("/events.previous.log") && !LittleFS.remove("/events.previous.log"))
        return File();
      if (!LittleFS.rename("/events.log", "/events.previous.log")) return File();
    }
  }
  return LittleFS.open("/events.log", FILE_APPEND);
}
}  // namespace

bool EventLog::begin() {
  if (!LittleFS.begin(false)) return false;
  File marker = LittleFS.open("/info.txt", FILE_READ);
  constexpr char expectedTag[] = "ACTUATOR-SIM\n";
  char tag[sizeof(expectedTag)] = {};
  const bool validMarker = marker &&
      marker.readBytes(tag, sizeof(expectedTag) - 1) == sizeof(expectedTag) - 1 &&
      std::strcmp(tag, expectedTag) == 0;
  marker.close();
  if (!validMarker) return false;

  File probe = LittleFS.open("/.health", FILE_WRITE);
  const uint8_t expected[] = {'O', 'K'};
  const bool written = probe && probe.write(expected, sizeof(expected)) == sizeof(expected);
  probe.flush();
  probe.close();
  char actual[2] = {};
  probe = LittleFS.open("/.health", FILE_READ);
  const bool readable = probe && probe.readBytes(actual, sizeof(actual)) == sizeof(actual);
  probe.close();
  const bool removed = LittleFS.remove("/.health");
  if (!written || !readable || !removed || std::memcmp(actual, expected, sizeof(actual)) != 0)
    return false;
  // Validate the actual journal path without rotating or erasing retained events.
  if (!journalPathsValid()) return false;
  File journal = LittleFS.open("/events.log", FILE_APPEND);
  const bool journalReady = journal && !journal.isDirectory();
  journal.close();
  return journalReady;
}

bool EventLog::recordDeployment(uint32_t now, uint32_t count) {
  char line[96];
  const int size = std::snprintf(line, sizeof(line), "UPTIME_MS:%lu,STATE:ACTUATED,COUNT:%lu\n",
      static_cast<unsigned long>(now), static_cast<unsigned long>(count));
  if (size < 0 || static_cast<std::size_t>(size) >= sizeof(line)) return false;
  const auto length = static_cast<std::size_t>(size);
  File file = prepareJournal(length);
  if (!file) return false;
  const std::size_t previousSize = file.size();
  const bool written = file.write(reinterpret_cast<const uint8_t*>(line), length) == length;
  file.flush();
  file.close();
  file = LittleFS.open("/events.log", FILE_READ);
  const bool persisted = file && file.size() == previousSize + length;
  file.close();
  return written && persisted;
}
