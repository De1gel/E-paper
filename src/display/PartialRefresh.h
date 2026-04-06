#ifndef PARTIAL_REFRESH_H
#define PARTIAL_REFRESH_H

#include <Arduino.h>

namespace partial_refresh {

void fillWindowSolid(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                     uint8_t color_nibble);
void writeWindowFromPacked(const uint8_t *packed_frame, uint16_t frame_width_px,
                           uint16_t x, uint16_t y, uint16_t width, uint16_t height);

}  // namespace partial_refresh

#endif
