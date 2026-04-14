#ifndef SCENE_RASTERIZER_H
#define SCENE_RASTERIZER_H

#include <Arduino.h>

#include "calendar/CalendarLayout.h"
#include "calendar/CalendarModel.h"
#include "render/StripeBuffer.h"

namespace render {

uint16_t calendarPhysicalWidth(const calendar::CalendarLayout &layout);
uint16_t calendarPhysicalHeight(const calendar::CalendarLayout &layout);
bool calendarLogicalToPhysical(const calendar::CalendarLayout &layout, uint16_t x, uint16_t y,
                               uint16_t &physical_x, uint16_t &physical_y);

bool rasterizeCalendarSceneStripe(const calendar::CalendarModel &model,
                                  const calendar::CalendarLayout &layout, uint16_t stripe_y,
                                  uint16_t stripe_rows, StripeBuffer &buffer);

}  // namespace render

#endif
