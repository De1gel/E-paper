#ifndef PARTIAL_REFRESH_H
#define PARTIAL_REFRESH_H

#include <Arduino.h>

namespace partial_refresh {

void fillWindowSolid(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                     uint8_t color_nibble);

}  // namespace partial_refresh

#endif
