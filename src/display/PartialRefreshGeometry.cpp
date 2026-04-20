#include "display/PartialRefreshGeometry.h"

namespace partial_refresh {

void normalizePartialWindow(uint16_t panel_width, uint16_t &x, uint16_t &width) {
  if (width == 0 || panel_width == 0 || x >= panel_width) {
    width = 0;
    return;
  }
  const uint16_t aligned_x =
      static_cast<uint16_t>(x & ~(static_cast<uint16_t>(kPartialWindowAlignPx - 1u)));
  const uint16_t grow_left = static_cast<uint16_t>(x - aligned_x);
  x = aligned_x;
  width = static_cast<uint16_t>(width + grow_left);
  const uint16_t remainder = static_cast<uint16_t>(width % kPartialWindowAlignPx);
  if (remainder != 0u) {
    width = static_cast<uint16_t>(width + (kPartialWindowAlignPx - remainder));
  }
  if (x + width > panel_width) {
    width = static_cast<uint16_t>(panel_width - x);
    width = static_cast<uint16_t>(width - (width % kPartialWindowAlignPx));
  }
}

}  // namespace partial_refresh
