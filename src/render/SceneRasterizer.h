#ifndef SCENE_RASTERIZER_H
#define SCENE_RASTERIZER_H

#include <Arduino.h>

#include "calendar/CalendarLayout.h"
#include "calendar/CalendarModel.h"
#include "render/StripeBuffer.h"

namespace render {

bool rasterizeCalendarSceneStripe(const calendar::CalendarModel &model,
                                  const calendar::CalendarLayout &layout, uint16_t stripe_y,
                                  uint16_t stripe_rows, StripeBuffer &buffer);

}  // namespace render

#endif
