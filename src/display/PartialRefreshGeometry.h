#ifndef DISPLAY_PARTIAL_REFRESH_GEOMETRY_H
#define DISPLAY_PARTIAL_REFRESH_GEOMETRY_H

#include <stdint.h>

namespace partial_refresh {

constexpr uint16_t kPartialWindowAlignPx = 4u;

void normalizePartialWindow(uint16_t panel_width, uint16_t &x, uint16_t &width);

}  // namespace partial_refresh

#endif
