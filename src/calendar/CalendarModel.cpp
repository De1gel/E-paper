#include "calendar/CalendarModel.h"

#include "Display_EPD_W21.h"
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
    model.schedule_title = "AGENDA";
    model.no_time_label = "PAS D HEURE";
    model.more_label = "PLUS";
    const char *labels[7] = {"DIM", "LUN", "MAR", "MER", "JEU", "VEN", "SAM"};
    for (uint8_t i = 0; i < 7; ++i) {
      model.weekday_labels[i] = labels[i];
    }
    return;
  }

  if (model.ui_language == "zh") {
    model.schedule_title = fallbackMissingGlyphs(kZhScheduleTitle, TextFont::Cjk24, "AN PAI");
    model.no_time_label =
        fallbackMissingGlyphs(kZhNoTimeLabel, TextFont::Cjk24, "WU SHI JIAN");
    model.more_label = fallbackMissingGlyphs(kZhMoreLabel, TextFont::Cjk16, "GENG DUO");
    for (uint8_t i = 0; i < 7; ++i) {
      model.weekday_labels[i] =
          fallbackMissingGlyphs(kZhWeekdayLabels[i], TextFont::Cjk16, kZhWeekdayFallbacks[i]);
    }
    return;
  }

  model.schedule_title = "SCHEDULE";
  model.no_time_label = "NO TIME";
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

bool calendarEventMatchesToday(const appfw::WifiManager::CalendarEvent &event, const String &today_ymd,
                               int today_weekday) {
  if (event.repeat == "daily") {
    return true;
  }
  if (event.repeat == "weekly") {
    return event.weekday == today_weekday;
  }
  return event.date == today_ymd;
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
    weather_label = fallbackMissingGlyphs(weather_label, TextFont::Cjk16, "TIAN QI");
  }
  model.header_weather = weather_label;

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
    appfw::WifiManager::CalendarEvent event;
    if (!wifi_manager.calendarEventAt(i, event)) {
      continue;
    }
    if (!time_valid || calendarEventMatchesToday(event, today_ymd, today_weekday)) {
      if (model.visible_event_count >= appfw::WifiManager::kMaxCalendarEvents) {
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
      dst.color_nibble = calendarColorToNibble(event.color);
    }
  }

  for (size_t i = 0; i + 1 < model.visible_event_count; ++i) {
    for (size_t j = i + 1; j < model.visible_event_count; ++j) {
      bool swap_needed = false;
      if (model.visible_events[j].time_hhmm < model.visible_events[i].time_hhmm) {
        swap_needed = true;
      } else if (model.visible_events[j].time_hhmm == model.visible_events[i].time_hhmm &&
                 model.visible_events[j].title < model.visible_events[i].title) {
        swap_needed = true;
      } else if (model.visible_events[j].time_hhmm == model.visible_events[i].time_hhmm &&
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

  for (size_t i = 0; i < model.visible_event_count; ++i) {
    if (model.schedule_group_count == 0 ||
        model.schedule_groups[model.schedule_group_count - 1].time_hhmm !=
            model.visible_events[i].time_hhmm) {
      if (model.schedule_group_count >= appfw::WifiManager::kMaxCalendarEvents) {
        break;
      }
      ScheduleGroup &group = model.schedule_groups[model.schedule_group_count++];
      group.time_hhmm = model.visible_events[i].time_hhmm;
      group.event_count = 0;
    }
    ScheduleGroup &group = model.schedule_groups[model.schedule_group_count - 1];
    if (group.event_count < appfw::WifiManager::kMaxCalendarEvents) {
      group.event_indices[group.event_count++] = static_cast<uint8_t>(i);
    }
  }

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
      cell.text_color = red;
    }
    if (cell.is_today) {
      cell.text_color = white;
    }
  }
}

}  // namespace calendar
