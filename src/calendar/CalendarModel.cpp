#include "calendar/CalendarModel.h"

#include <algorithm>
#include <string.h>

#include "Display_EPD_W21.h"
#include "calendar/CalendarLogic.h"
#include "calendar/CalendarText.h"

namespace calendar {
namespace {

constexpr const char kZhScheduleTitle[] = "\xE5\xAE\x89\xE6\x8E\x92";
constexpr const char kZhNoTimeLabel[] = "\xE6\x97\xA0\xE6\x97\xB6\xE9\x97\xB4";
constexpr const char kZhMoreLabel[] = "\xE6\x9B\xB4\xE5\xA4\x9A";
constexpr const char *kZhWeekdayLabels[] = {
    "\xE6\x97\xA5", "\xE4\xB8\x80", "\xE4\xBA\x8C", "\xE4\xB8\x89",
    "\xE5\x9B\x9B", "\xE4\xBA\x94", "\xE5\x85\xAD",
};
constexpr const char *kZhWeekdayFallbacks[] = {"RI", "YI", "ER", "SAN", "SI", "WU", "LIU"};

bool isLeapYear(int year) {
  return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

int daysInMonth(int year, int month) {
  static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year)) {
    return 29;
  }
  if (month < 1 || month > 12) {
    return 30;
  }
  return kDays[month - 1];
}

String twoDigits(int value) {
  if (value < 10) {
    return "0" + String(value);
  }
  return String(value);
}

String formatDateYmd(const struct tm &local_tm) {
  return String(local_tm.tm_year + 1900) + "-" + twoDigits(local_tm.tm_mon + 1) + "-" +
         twoDigits(local_tm.tm_mday);
}

String formatTimeHm(const struct tm &local_tm) {
  return twoDigits(local_tm.tm_hour) + ":" + twoDigits(local_tm.tm_min);
}

String formatYmd(int year, int month, int day) {
  return String(year) + "-" + twoDigits(month) + "-" + twoDigits(day);
}

bool isAsciiOnlyText(const String &text) {
  for (size_t i = 0; i < text.length(); ++i) {
    if (static_cast<uint8_t>(text.charAt(i)) >= 0x80u) {
      return false;
    }
  }
  return true;
}

size_t utf8PrefixBytes(const String &text, size_t codepoint_limit) {
  size_t byte_index = 0;
  size_t count = 0;
  while (byte_index < text.length() && count < codepoint_limit) {
    const uint8_t first = static_cast<uint8_t>(text[byte_index]);
    size_t advance = 1;
    if ((first & 0x80u) == 0u) {
      advance = 1;
    } else if ((first & 0xE0u) == 0xC0u) {
      advance = 2;
    } else if ((first & 0xF0u) == 0xE0u) {
      advance = 3;
    } else if ((first & 0xF8u) == 0xF0u) {
      advance = 4;
    }
    if (byte_index + advance > text.length()) {
      break;
    }
    byte_index += advance;
    ++count;
  }
  return byte_index;
}

String summarizeAsciiTitle(const String &title) {
  String normalized = sanitizeDisplayText(title, "ITEM");
  normalized.trim();
  if (normalized.length() == 0) {
    return "ITEM";
  }

  size_t start = 0;
  while (start < normalized.length() && normalized.charAt(start) == ' ') {
    ++start;
  }
  size_t end = start;
  while (end < normalized.length() && normalized.charAt(end) != ' ') {
    ++end;
  }
  String first_word = normalized.substring(start, end);
  first_word.trim();
  if (first_word.length() == 0) {
    first_word = normalized;
  }
  if (first_word.length() <= 5) {
    return first_word;
  }
  return first_word.substring(0, 5);
}

String summarizeCjkTitle(const String &title) {
  String normalized = fallbackMissingGlyphs(title, TextFont::Cjk10, "ITEM");
  normalized.trim();
  if (normalized.length() == 0) {
    return "ITEM";
  }
  return normalized.substring(0, utf8PrefixBytes(normalized, 3));
}

String summarizeEventTitle(const String &title) {
  return isAsciiOnlyText(title) ? summarizeAsciiTitle(title) : summarizeCjkTitle(title);
}

bool parseHmToMinutes(const String &value, uint16_t &minutes_out) {
  String normalized = value;
  normalized.trim();
  if (normalized.length() != 5 || normalized[2] != ':') {
    return false;
  }
  if (!isDigit(normalized[0]) || !isDigit(normalized[1]) || !isDigit(normalized[3]) ||
      !isDigit(normalized[4])) {
    return false;
  }
  const int hour = (normalized[0] - '0') * 10 + (normalized[1] - '0');
  const int minute = (normalized[3] - '0') * 10 + (normalized[4] - '0');
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
    return false;
  }
  minutes_out = static_cast<uint16_t>(hour * 60 + minute);
  return true;
}

int32_t dayKeyFromTm(const struct tm &t) {
  return static_cast<int32_t>((t.tm_year + 1900) * 1000 + t.tm_yday);
}

String normalizeUiLanguage(const String &raw) {
  String lang = raw;
  lang.trim();
  lang.toLowerCase();
  if (lang == "en" || lang == "fr") {
    return lang;
  }
  return "zh";
}

void fillUiStrings(CalendarModel &model) {
  if (model.ui_language == "fr") {
    model.schedule_title = "";
    model.no_time_label = "";
    model.more_label = "PLUS";
    const char *labels[7] = {"DIM", "LUN", "MAR", "MER", "JEU", "VEN", "SAM"};
    for (uint8_t i = 0; i < 7; ++i) {
      model.weekday_labels[i] = labels[i];
    }
    return;
  }

  if (model.ui_language == "zh") {
    model.schedule_title = "";
    model.no_time_label = "";
    model.more_label = fallbackMissingGlyphs(kZhMoreLabel, TextFont::Cjk16, "GENG DUO");
    for (uint8_t i = 0; i < 7; ++i) {
      model.weekday_labels[i] =
          fallbackMissingGlyphs(kZhWeekdayLabels[i], TextFont::Cjk26, kZhWeekdayFallbacks[i]);
    }
    return;
  }

  model.schedule_title = "";
  model.no_time_label = "";
  model.more_label = "MORE";
  const char *labels[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  for (uint8_t i = 0; i < 7; ++i) {
    model.weekday_labels[i] = labels[i];
  }
}

uint8_t calendarColorToNibble(const String &raw) {
  String color = raw;
  color.toLowerCase();
  if (color == "black") return black;
  if (color == "white") return white;
  if (color == "yellow") return yellow;
  if (color == "red") return red;
  if (color == "blue") return blue;
  if (color == "green") return green;
  return blue;
}

uint8_t dayIndicatorColorNibble(const String &raw) {
  const uint8_t nibble = calendarColorToNibble(raw);
  return (nibble == white) ? black : nibble;
}

void appendDaySummaryItem(DaySummary &summary, uint8_t color_nibble, const String &label) {
  if (summary.item_count < DaySummary::kMaxItems) {
    DaySummary::Item &item = summary.items[summary.item_count++];
    item.color_nibble = color_nibble;
    memset(item.label, 0, sizeof(item.label));
    const size_t copy_len =
        std::min(static_cast<size_t>(label.length()), sizeof(item.label) - 1u);
    for (size_t i = 0; i < copy_len; ++i) {
      item.label[i] = label.charAt(i);
    }
    return;
  }
  ++summary.hidden_count;
}

constexpr uint16_t kScheduleStartMinute = 8u * 60u;
constexpr uint16_t kScheduleEndMinute = 22u * 60u;

void assignTimelineLanes(CalendarModel &model) {
  if (model.visible_event_count == 0) {
    return;
  }
  TimelineEventSlot slots[appfw::kMaxCalendarEvents] = {};
  for (size_t i = 0; i < model.visible_event_count; ++i) {
    slots[i].start_minute = model.visible_events[i].start_minute;
    slots[i].end_minute = model.visible_events[i].end_minute;
    slots[i].lane = model.visible_events[i].lane;
    slots[i].lane_count = model.visible_events[i].lane_count;
  }
  assignTimelineLanes(slots, model.visible_event_count, appfw::kMaxCalendarEvents);
  for (size_t i = 0; i < model.visible_event_count; ++i) {
    model.visible_events[i].lane = slots[i].lane;
    model.visible_events[i].lane_count = slots[i].lane_count;
  }
}

bool isWeekendColumn(int col) {
  return col == 0 || col == 6;
}

}  // namespace

void buildCalendarModel(CalendarModel &model, const struct tm &local_tm, bool time_valid,
                        LayoutMode layout_mode, const String &ui_language,
                        const appfw::WifiManager &wifi_manager) {
  model = CalendarModel{};
  model.time_valid = time_valid;
  model.layout_mode = layout_mode;
  model.current_minute_of_day = static_cast<uint16_t>(local_tm.tm_hour * 60 + local_tm.tm_min);
  model.ui_language = normalizeUiLanguage(ui_language);
  fillUiStrings(model);
  model.title = "-- -- --";
  model.title = String(local_tm.tm_year + 1900) + "-" + twoDigits(local_tm.tm_mon + 1) + "-" +
                twoDigits(local_tm.tm_mday);
  model.header_date = model.title;
  model.header_time = formatTimeHm(local_tm);

  String weather_label = wifi_manager.weatherCity();
  weather_label.trim();
  if (weather_label.length() == 0) {
    weather_label = (model.ui_language == "zh") ? "TIAN QI" : "WEATHER";
  } else if (model.ui_language == "zh") {
    weather_label = fallbackMissingGlyphs(weather_label, TextFont::Cjk30, "TIAN QI");
  }
  model.header_weather = weather_label;
  model.header_weather_code = static_cast<int16_t>(wifi_manager.weatherCode());

  const float temperature_c = wifi_manager.temperatureC();
  const float humidity_pct = wifi_manager.humidityPct();
  if (isnan(temperature_c) || isnan(humidity_pct)) {
    model.header_sensors = "--.-C --%";
  } else {
    model.header_sensors = String(temperature_c, 1) + "C " + String(humidity_pct, 0) + "%";
  }

  const int today_weekday = time_valid ? ((local_tm.tm_wday + 6) % 7) : -1;
  const String today_ymd = time_valid ? formatDateYmd(local_tm) : "";
  for (size_t i = 0; i < wifi_manager.calendarEventCount(); ++i) {
    appfw::CalendarEvent event;
    if (!wifi_manager.calendarEventAt(i, event)) {
      continue;
    }
    if (!time_valid || calendar::calendarEventMatchesToday(event.repeat.c_str(), event.weekday,
                                                           event.date.c_str(), today_ymd.c_str(),
                                                           today_weekday)) {
      if (model.visible_event_count >= appfw::kMaxCalendarEvents) {
        break;
      }
      VisibleEvent &dst = model.visible_events[model.visible_event_count++];
      dst.id = event.id;
      dst.title = event.title;
      dst.title.trim();
      if (dst.title.length() == 0) {
        dst.title = "ITEM";
      } else if (model.ui_language == "zh") {
        dst.title = fallbackMissingGlyphs(dst.title, TextFont::Cjk16, "ITEM");
      } else {
        dst.title = sanitizeDisplayText(dst.title, "ITEM");
      }
      dst.time_hhmm = event.time_hhmm;
      dst.end_time_hhmm = event.end_time_hhmm;
      uint16_t start_minute = 0;
      if (!parseHmToMinutes(event.time_hhmm, start_minute)) {
        start_minute = kScheduleStartMinute;
      }
      uint16_t end_minute = static_cast<uint16_t>(start_minute + 30u);
      uint16_t parsed_end = 0;
      if (parseHmToMinutes(event.end_time_hhmm, parsed_end) && parsed_end > start_minute) {
        end_minute = parsed_end;
      }
      if (end_minute <= start_minute) {
        end_minute = static_cast<uint16_t>(start_minute + 30u);
      }
      dst.start_minute = start_minute;
      dst.end_minute = end_minute;
      dst.color_nibble = calendarColorToNibble(event.color);
    }
  }

  for (size_t i = 0; i + 1 < model.visible_event_count; ++i) {
    for (size_t j = i + 1; j < model.visible_event_count; ++j) {
      bool swap_needed = false;
      if (model.visible_events[j].start_minute < model.visible_events[i].start_minute) {
        swap_needed = true;
      } else if (model.visible_events[j].start_minute == model.visible_events[i].start_minute &&
                 model.visible_events[j].end_minute > model.visible_events[i].end_minute) {
        swap_needed = true;
      } else if (model.visible_events[j].start_minute == model.visible_events[i].start_minute &&
                 model.visible_events[j].title < model.visible_events[i].title) {
        swap_needed = true;
      } else if (model.visible_events[j].start_minute == model.visible_events[i].start_minute &&
                 model.visible_events[j].title == model.visible_events[i].title &&
                 model.visible_events[j].id < model.visible_events[i].id) {
        swap_needed = true;
      }
      if (swap_needed) {
        VisibleEvent tmp = model.visible_events[i];
        model.visible_events[i] = model.visible_events[j];
        model.visible_events[j] = tmp;
      }
    }
  }
  assignTimelineLanes(model);

  if (!time_valid) {
    return;
  }

  const int year = local_tm.tm_year + 1900;
  const int month = local_tm.tm_mon + 1;
  const int today = local_tm.tm_mday;
  struct tm first_day = local_tm;
  first_day.tm_mday = 1;
  first_day.tm_hour = 12;
  first_day.tm_min = 0;
  first_day.tm_sec = 0;
  mktime(&first_day);

  const int first_col = first_day.tm_wday;
  const int cur_days = daysInMonth(year, month);
  const int required_rows = (first_col + cur_days + 6) / 7;
  model.month_row_count =
      static_cast<uint8_t>(required_rows < 4 ? 4 : (required_rows > 6 ? 6 : required_rows));
  const int prev_month = (month == 1) ? 12 : (month - 1);
  const int prev_year = (month == 1) ? (year - 1) : year;
  const int prev_days = daysInMonth(prev_year, prev_month);

  for (int index = 0; index < 42; ++index) {
    const int col = index % 7;
    DateCell &cell = model.date_cells[index];
    if (index < first_col) {
      cell.day = prev_days - (first_col - index - 1);
    } else if (index < (first_col + cur_days)) {
      cell.day = index - first_col + 1;
      cell.in_current = true;
    } else {
      cell.day = index - (first_col + cur_days) + 1;
    }

    cell.is_today = cell.in_current && (cell.day == today);
    cell.text_color = cell.in_current ? black : blue;
    if (isWeekendColumn(col)) {
      cell.text_color = blue;
    }
    if (cell.is_today) {
      cell.text_color = white;
    }
  }

  for (int index = 0; index < 42; ++index) {
    DateCell &cell = model.date_cells[index];
    if (!cell.in_current) {
      continue;
    }
    const int day = cell.day;
    const String day_ymd = formatYmd(year, month, day);
    struct tm day_tm = local_tm;
    day_tm.tm_mday = day;
    day_tm.tm_hour = 12;
    day_tm.tm_min = 0;
    day_tm.tm_sec = 0;
    mktime(&day_tm);
    const int day_weekday = (day_tm.tm_wday + 6) % 7;
    DaySummary &summary = model.day_summaries[index];
    struct SummaryCandidate {
      uint16_t start_minute = 0;
      uint8_t color_nibble = black;
      String label;
    };
    SummaryCandidate candidates[DaySummary::kMaxItems] = {};
    uint8_t candidate_count = 0;
    uint8_t total_matches = 0;
    for (size_t event_index = 0; event_index < wifi_manager.calendarEventCount(); ++event_index) {
      appfw::CalendarEvent event;
      if (!wifi_manager.calendarEventAt(event_index, event)) {
        continue;
      }
      if (!calendar::calendarEventMatchesToday(event.repeat.c_str(), event.weekday, event.date.c_str(),
                                               day_ymd.c_str(), day_weekday)) {
        continue;
      }
      ++total_matches;
      uint16_t start_minute = 24u * 60u;
      parseHmToMinutes(event.time_hhmm, start_minute);
      SummaryCandidate candidate;
      candidate.start_minute = start_minute;
      candidate.color_nibble = dayIndicatorColorNibble(event.color);
      candidate.label = summarizeEventTitle(event.title);

      uint8_t insert_at = candidate_count;
      for (uint8_t i = 0; i < candidate_count; ++i) {
        if (candidate.start_minute < candidates[i].start_minute ||
            (candidate.start_minute == candidates[i].start_minute &&
             candidate.label < candidates[i].label)) {
          insert_at = i;
          break;
        }
      }
      if (insert_at < DaySummary::kMaxItems) {
        const uint8_t limit =
            (candidate_count < DaySummary::kMaxItems) ? static_cast<uint8_t>(candidate_count + 1u)
                                                      : DaySummary::kMaxItems;
        for (uint8_t move = limit; move > insert_at + 1u; --move) {
          candidates[move - 1u] = candidates[move - 2u];
        }
        candidates[insert_at] = candidate;
        if (candidate_count < DaySummary::kMaxItems) {
          ++candidate_count;
        }
      }
    }
    for (uint8_t i = 0; i < candidate_count; ++i) {
      appendDaySummaryItem(summary, candidates[i].color_nibble, candidates[i].label);
    }
    if (total_matches > summary.item_count) {
      summary.hidden_count = static_cast<uint8_t>(total_matches - summary.item_count);
    }
  }
}

}  // namespace calendar
