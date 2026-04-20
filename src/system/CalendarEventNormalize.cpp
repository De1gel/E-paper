#include "system/CalendarEventNormalize.h"

namespace appfw {
namespace {

int daysInMonth(int year, int month) {
  static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2) {
    const bool leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
    return leap ? 29 : 28;
  }
  if (month < 1 || month > 12) {
    return 30;
  }
  return kDays[month - 1];
}

bool isAsciiDigit(char c) {
  return c >= '0' && c <= '9';
}

}  // namespace

bool normalizeCalendarTimeValue(const String &raw, String &normalized) {
  String value = raw;
  value.trim();
  if (value.length() != 5 || value[2] != ':') {
    return false;
  }
  if (!isAsciiDigit(value[0]) || !isAsciiDigit(value[1]) ||
      !isAsciiDigit(value[3]) || !isAsciiDigit(value[4])) {
    return false;
  }
  const int hh = (value[0] - '0') * 10 + (value[1] - '0');
  const int mm = (value[3] - '0') * 10 + (value[4] - '0');
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) {
    return false;
  }
  normalized = value;
  return true;
}

bool normalizeCalendarDateValue(const String &raw, String &normalized) {
  String value = raw;
  value.trim();
  if (value.length() != 10 || value[4] != '-' || value[7] != '-') {
    return false;
  }
  for (int i = 0; i < 10; ++i) {
    if (i == 4 || i == 7) {
      continue;
    }
    if (!isAsciiDigit(value[i])) {
      return false;
    }
  }
  const int year = value.substring(0, 4).toInt();
  const int month = value.substring(5, 7).toInt();
  const int day = value.substring(8, 10).toInt();
  if (year < 2000 || year > 2099) {
    return false;
  }
  if (month < 1 || month > 12) {
    return false;
  }
  const int max_day = daysInMonth(year, month);
  if (day < 1 || day > max_day) {
    return false;
  }
  normalized = value;
  return true;
}

String normalizeCalendarColorValue(const String &raw) {
  String value = raw;
  value.trim();
  value.toLowerCase();
  if (value == "black" || value == "white" || value == "yellow" || value == "red" ||
      value == "blue" || value == "green") {
    return value;
  }
  return "blue";
}

String normalizeCalendarRepeatValue(const String &raw) {
  String value = raw;
  value.trim();
  value.toLowerCase();
  if (value == "once" || value == "daily" || value == "weekly") {
    return value;
  }
  return "weekly";
}

String normalizeCalendarSourceValue(const String &raw) {
  String value = raw;
  value.trim();
  value.toLowerCase();
  if (value.length() == 0) {
    return "manual";
  }
  String out;
  out.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.') {
      out += c;
    }
  }
  if (out.length() == 0) {
    out = "manual";
  }
  if (out.length() > 24) {
    out = out.substring(0, 24);
  }
  return out;
}

String normalizeCalendarExternalIdValue(const String &raw) {
  String value = raw;
  value.trim();
  if (value.length() > 80) {
    value = value.substring(0, 80);
  }
  return value;
}

String normalizeCalendarUpdatedAtValue(const String &raw) {
  String value = raw;
  value.trim();
  if (value.length() > 32) {
    value = value.substring(0, 32);
  }
  return value;
}

}  // namespace appfw
