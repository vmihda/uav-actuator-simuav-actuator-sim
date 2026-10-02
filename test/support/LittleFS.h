#pragma once

#include <algorithm>
#include <cstring>
#include <map>
#include <string>

constexpr const char* FILE_READ = "r";
constexpr const char* FILE_WRITE = "w";
constexpr const char* FILE_APPEND = "a";

class File {
 public:
  File() = default;
  File(std::string* data, bool writable, bool writeFails, bool directory = false)
      : data_(data), writable_(writable), writeFails_(writeFails), directory_(directory) {}
  explicit operator bool() const { return data_ != nullptr; }
  std::size_t size() const { return data_ ? data_->size() : 0; }
  bool isDirectory() const { return directory_; }
  std::size_t write(const uint8_t* bytes, std::size_t size) {
    if (!data_ || !writable_ || writeFails_ || directory_) return 0;
    data_->append(reinterpret_cast<const char*>(bytes), size);
    return size;
  }
  std::size_t readBytes(char* buffer, std::size_t size) {
    if (!data_) return 0;
    const auto count = std::min(size, data_->size());
    std::memcpy(buffer, data_->data(), count);
    return count;
  }
  void flush() {}
  void close() { data_ = nullptr; }
 private:
  std::string* data_ = nullptr;
  bool writable_ = false;
  bool writeFails_ = false;
  bool directory_ = false;
};

class LittleFSClass {
 public:
  bool begin(bool formatOnFail) { autoFormatted = formatOnFail; return mountWorks; }
  bool exists(const char* path) const { return files.count(path) != 0; }
  File open(const char* path, const char* mode) {
    const bool writable = std::strcmp(mode, FILE_READ) != 0;
    if (!mountWorks || blockedPath == path || (!writable && !exists(path))) return File();
    if (std::strcmp(mode, FILE_WRITE) == 0) files[path].clear();
    return File(&files[path], writable, writesFail, directoryPath == path);
  }
  bool remove(const char* path) { return files.erase(path) > 0; }
  bool rename(const char* oldPath, const char* newPath) {
    if (!exists(oldPath)) return false;
    files[newPath] = files[oldPath]; files.erase(oldPath); return true;
  }
  std::map<std::string, std::string> files;
  bool mountWorks = true;
  bool writesFail = false;
  bool autoFormatted = false;
  std::string blockedPath;
  std::string directoryPath;
};
extern LittleFSClass LittleFS;
