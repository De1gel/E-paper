#ifndef TEST_SUPPORT_ARDUINO_H
#define TEST_SUPPORT_ARDUINO_H

#include <stdint.h>
#include <stdlib.h>

#include <algorithm>
#include <cctype>
#include <string>

class String {
 public:
  String() = default;
  String(const char *value) : value_(value ? value : "") {}
  String(const std::string &value) : value_(value) {}
  String(char value) : value_(1, value) {}
  String(unsigned long value) : value_(std::to_string(value)) {}
  String(int value) : value_(std::to_string(value)) {}

  size_t length() const { return value_.size(); }
  void reserve(size_t n) { value_.reserve(n); }
  const char *c_str() const { return value_.c_str(); }

  String &operator=(const char *value) {
    value_ = value ? value : "";
    return *this;
  }

  String &operator+=(const String &other) {
    value_ += other.value_;
    return *this;
  }
  String &operator+=(const char *other) {
    value_ += (other ? other : "");
    return *this;
  }
  String &operator+=(char c) {
    value_ += c;
    return *this;
  }

  friend String operator+(const String &lhs, const String &rhs) {
    return String(lhs.value_ + rhs.value_);
  }

  bool operator==(const String &other) const { return value_ == other.value_; }
  bool operator==(const char *other) const { return value_ == (other ? other : ""); }
  bool operator!=(const String &other) const { return value_ != other.value_; }
  bool operator!=(const char *other) const { return value_ != (other ? other : ""); }

  char operator[](size_t index) const { return value_[index]; }

  int indexOf(const char *needle) const {
    const std::string query = needle ? needle : "";
    const size_t pos = value_.find(query);
    return (pos == std::string::npos) ? -1 : static_cast<int>(pos);
  }

  int indexOf(char needle) const {
    const size_t pos = value_.find(needle);
    return (pos == std::string::npos) ? -1 : static_cast<int>(pos);
  }

  int indexOf(char needle, int fromIndex) const {
    const size_t start = (fromIndex < 0) ? 0u : static_cast<size_t>(fromIndex);
    const size_t pos = value_.find(needle, start);
    return (pos == std::string::npos) ? -1 : static_cast<int>(pos);
  }

  int indexOf(const char *needle, int fromIndex) const {
    const std::string query = needle ? needle : "";
    const size_t start = (fromIndex < 0) ? 0u : static_cast<size_t>(fromIndex);
    const size_t pos = value_.find(query, start);
    return (pos == std::string::npos) ? -1 : static_cast<int>(pos);
  }

  void trim() {
    const size_t start = value_.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
      value_.clear();
      return;
    }
    const size_t end = value_.find_last_not_of(" \t\r\n");
    value_ = value_.substr(start, end - start + 1);
  }

  String substring(size_t start) const {
    if (start >= value_.size()) {
      return String("");
    }
    return String(value_.substr(start));
  }

  String substring(size_t start, size_t end) const {
    if (start >= value_.size() || end <= start) {
      return String("");
    }
    const size_t clamped_end = std::min(end, value_.size());
    return String(value_.substr(start, clamped_end - start));
  }

  long toInt() const {
    return strtol(value_.c_str(), nullptr, 10);
  }

  void toLowerCase() {
    std::transform(value_.begin(), value_.end(), value_.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  }

  void toUpperCase() {
    std::transform(value_.begin(), value_.end(), value_.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  }

  bool endsWith(const char *suffix) const {
    const std::string tail = suffix ? suffix : "";
    if (tail.size() > value_.size()) {
      return false;
    }
    return value_.compare(value_.size() - tail.size(), tail.size(), tail) == 0;
  }

  bool startsWith(const char *prefix) const {
    const std::string head = prefix ? prefix : "";
    if (head.size() > value_.size()) {
      return false;
    }
    return value_.compare(0, head.size(), head) == 0;
  }

  void replace(const char *from, const char *to) {
    const std::string from_s = from ? from : "";
    const std::string to_s = to ? to : "";
    if (from_s.empty()) {
      return;
    }
    size_t pos = 0;
    while ((pos = value_.find(from_s, pos)) != std::string::npos) {
      value_.replace(pos, from_s.size(), to_s);
      pos += to_s.size();
    }
  }

 private:
  std::string value_;
};

#endif
