#include "system/CalendarStore.h"

#include "system/CalendarEventNormalize.h"

namespace appfw {
namespace {

char hexDigit(uint8_t value) {
  return (value < 10u) ? static_cast<char>('0' + value)
                       : static_cast<char>('A' + (value - 10u));
}

}  // namespace

bool CalendarStore::eventAt(size_t index, CalendarEvent &event) const {
  if (index >= count_) {
    return false;
  }
  event = events_[index];
  return true;
}

void CalendarStore::clear() {
  count_ = 0;
  next_id_ = 1;
}

void CalendarStore::setNextId(uint16_t next_id) {
  next_id_ = (next_id == 0) ? 1 : next_id;
}

uint16_t CalendarStore::allocateId() {
  const uint16_t allocated = next_id_;
  ++next_id_;
  if (next_id_ == 0) {
    next_id_ = 1;
  }
  return allocated;
}

int CalendarStore::findIndexById(uint16_t id) const {
  for (size_t i = 0; i < count_; ++i) {
    if (events_[i].id == id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int CalendarStore::findIndexByExternal(const String &source, const String &external_id) const {
  if (source.length() == 0 || external_id.length() == 0) {
    return -1;
  }
  for (size_t i = 0; i < count_; ++i) {
    if (events_[i].source == source && events_[i].external_id == external_id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool CalendarStore::removeAt(size_t index) {
  if (index >= count_) {
    return false;
  }
  for (size_t i = index; i + 1 < count_; ++i) {
    events_[i] = events_[i + 1];
  }
  if (count_ > 0) {
    --count_;
  }
  return true;
}

bool CalendarStore::push(const CalendarEvent &event) {
  if (count_ >= kMaxCalendarEvents) {
    return false;
  }
  events_[count_++] = event;
  return true;
}

void CalendarStore::replaceAll(const CalendarEvent *events, size_t count) {
  count_ = (count > kMaxCalendarEvents) ? kMaxCalendarEvents : count;
  for (size_t i = 0; i < count_; ++i) {
    events_[i] = events[i];
  }
}

String CalendarStore::serialize() const {
  String out;
  for (size_t i = 0; i < count_; ++i) {
    const CalendarEvent &e = events_[i];
    if (i > 0) {
      out += "\n";
    }
    out += String(e.id);
    out += "|";
    out += urlEncode(e.date);
    out += "|";
    out += urlEncode(e.time_hhmm);
    out += "|";
    out += urlEncode(e.end_time_hhmm);
    out += "|";
    out += urlEncode(e.color);
    out += "|";
    out += urlEncode(e.repeat);
    out += "|";
    out += String(e.weekday);
    out += "|";
    out += urlEncode(e.title);
    out += "|";
    out += urlEncode(e.source);
    out += "|";
    out += urlEncode(e.external_id);
    out += "|";
    out += urlEncode(e.updated_at);
  }
  return out;
}

String CalendarStore::toJson() const {
  String json = "{\"ok\":true,\"items\":[";
  for (size_t i = 0; i < count_; ++i) {
    const CalendarEvent &e = events_[i];
    if (i > 0) {
      json += ",";
    }
    json += "{";
    json += "\"id\":";
    json += String(e.id);
    json += ",\"title\":\"";
    json += jsonEscape(e.title);
    json += "\",\"date\":\"";
    json += jsonEscape(e.date);
    json += "\",\"time\":\"";
    json += jsonEscape(e.time_hhmm);
    json += "\",\"end_time\":\"";
    json += jsonEscape(e.end_time_hhmm);
    json += "\",\"color\":\"";
    json += jsonEscape(e.color);
    json += "\",\"repeat\":\"";
    json += jsonEscape(e.repeat);
    json += "\",\"weekday\":";
    json += String(e.weekday);
    json += ",\"source\":\"";
    json += jsonEscape(e.source);
    json += "\",\"external_id\":\"";
    json += jsonEscape(e.external_id);
    json += "\",\"updated_at\":\"";
    json += jsonEscape(e.updated_at);
    json += "}";
  }
  json += "]}";
  return json;
}

void CalendarStore::deserialize(const String &packed) {
  clear();
  uint16_t max_id = 0;
  int start = 0;
  while (start <= static_cast<int>(packed.length()) &&
         count_ < static_cast<size_t>(kMaxCalendarEvents)) {
    int end = packed.indexOf("\n", start);
    if (end < 0) {
      end = packed.length();
    }
    String line = packed.substring(start, end);
    line.trim();
    start = end + 1;
    if (line.length() == 0) {
      continue;
    }

    String fields[11];
    int field_count = 0;
    int field_start = 0;
    while (field_start <= static_cast<int>(line.length()) && field_count < 11) {
      const int sep = line.indexOf("|", field_start);
      if (sep < 0) {
        fields[field_count++] = line.substring(field_start);
        field_start = static_cast<int>(line.length()) + 1;
        break;
      }
      fields[field_count++] = line.substring(field_start, sep);
      field_start = sep + 1;
    }
    if (!(field_count == 7 || field_count == 11)) {
      continue;
    }

    CalendarEvent event;
    event.id = static_cast<uint16_t>(fields[0].toInt());
    event.date = urlDecode(fields[1]);
    event.time_hhmm = urlDecode(fields[2]);
    if (field_count >= 11) {
      event.end_time_hhmm = urlDecode(fields[3]);
      event.color = urlDecode(fields[4]);
      event.repeat = urlDecode(fields[5]);
      event.weekday = static_cast<int8_t>(fields[6].toInt());
      event.title = urlDecode(fields[7]);
      event.source = urlDecode(fields[8]);
      event.external_id = urlDecode(fields[9]);
      event.updated_at = urlDecode(fields[10]);
    } else {
      event.end_time_hhmm = "";
      event.color = urlDecode(fields[3]);
      event.repeat = urlDecode(fields[4]);
      event.weekday = static_cast<int8_t>(fields[5].toInt());
      event.title = urlDecode(fields[6]);
      event.source = "manual";
      event.external_id = "";
      event.updated_at = "";
    }
    event.title.trim();

    String normalized_time;
    String normalized_end_time;
    String normalized_date;
    if (!normalizeCalendarTimeValue(event.time_hhmm, normalized_time)) {
      continue;
    }
    event.time_hhmm = normalized_time;
    if (event.end_time_hhmm.length() > 0) {
      if (!normalizeCalendarTimeValue(event.end_time_hhmm, normalized_end_time)) {
        event.end_time_hhmm = "";
      } else {
        event.end_time_hhmm = normalized_end_time;
      }
    }
    event.color = normalizeCalendarColorValue(event.color);
    event.repeat = normalizeCalendarRepeatValue(event.repeat);
    event.source = normalizeCalendarSourceValue(event.source);
    event.external_id = normalizeCalendarExternalIdValue(event.external_id);
    event.updated_at = normalizeCalendarUpdatedAtValue(event.updated_at);
    if (event.repeat == "once") {
      if (!normalizeCalendarDateValue(event.date, normalized_date)) {
        continue;
      }
      event.date = normalized_date;
      event.weekday = -1;
    } else if (event.repeat == "weekly") {
      if (event.weekday < 0 || event.weekday > 6) {
        continue;
      }
      event.date = "";
    } else {
      event.weekday = -1;
      event.date = "";
    }
    if (event.id == 0) {
      continue;
    }
    if (event.title.length() == 0) {
      event.title = "Event";
    }
    if (event.id > max_id) {
      max_id = event.id;
    }
    push(event);
  }

  if (next_id_ <= max_id) {
    setNextId(static_cast<uint16_t>(max_id + 1));
  }
}

String CalendarStore::jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); ++i) {
    const char c = s[i];
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

String CalendarStore::urlEncode(const String &s) {
  String out;
  out.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hexDigit(static_cast<uint8_t>(c >> 4));
      out += hexDigit(static_cast<uint8_t>(c & 0x0Fu));
    }
  }
  return out;
}

String CalendarStore::urlDecode(const String &s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); ++i) {
    const char c = s[i];
    if (c == '%' && (i + 2) < s.length()) {
      const char hi = s[i + 1];
      const char lo = s[i + 2];
      auto hexValue = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
      };
      const int hi_val = hexValue(hi);
      const int lo_val = hexValue(lo);
      if (hi_val >= 0 && lo_val >= 0) {
        out += static_cast<char>((hi_val << 4) | lo_val);
        i += 2;
        continue;
      }
    }
    if (c == '+') {
      out += ' ';
    } else {
      out += c;
    }
  }
  return out;
}

}  // namespace appfw
