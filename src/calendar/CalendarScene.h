#ifndef CALENDAR_SCENE_H
#define CALENDAR_SCENE_H

#include <Arduino.h>

#include "calendar/CalendarLayout.h"
#include "calendar/CalendarModel.h"
#include "calendar/CalendarText.h"

namespace calendar {

class SceneSink {
 public:
  virtual ~SceneSink() = default;
  virtual void fillRect(const Rect &rect, uint8_t color_nibble) = 0;
  virtual void strokeRect(const Rect &rect, uint8_t color_nibble) = 0;
  virtual void text(uint16_t x, uint16_t y, const String &text, uint8_t pixel_height,
                    uint8_t color_nibble, TextFont font = TextFont::Auto,
                    TextAAMode aa_mode = TextAAMode::Threshold) = 0;
};

void emitCalendarWeatherHeader(const CalendarModel &model, const CalendarLayout &layout,
                               SceneSink &sink);
void emitCalendarScene(const CalendarModel &model, const CalendarLayout &layout, SceneSink &sink);

}  // namespace calendar

#endif
