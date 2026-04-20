#ifndef SYSTEM_CALENDAR_STORE_H
#define SYSTEM_CALENDAR_STORE_H

#include <Arduino.h>

#include "system/CalendarData.h"

namespace appfw {

class CalendarStore {
 public:
  size_t count() const { return count_; }
  bool eventAt(size_t index, CalendarEvent &event) const;
  CalendarEvent *data() { return events_; }
  const CalendarEvent *data() const { return events_; }
  void clear();

  uint16_t nextId() const { return next_id_; }
  void setNextId(uint16_t next_id);
  uint16_t allocateId();

  int findIndexById(uint16_t id) const;
  int findIndexByExternal(const String &source, const String &external_id) const;
  bool removeAt(size_t index);
  bool push(const CalendarEvent &event);
  void replaceAll(const CalendarEvent *events, size_t count);

  String serialize() const;
  void deserialize(const String &packed);
  String toJson() const;

 private:
  static String jsonEscape(const String &s);
  static String urlEncode(const String &s);
  static String urlDecode(const String &s);

  CalendarEvent events_[kMaxCalendarEvents];
  size_t count_ = 0;
  uint16_t next_id_ = 1;
};

}  // namespace appfw

#endif
