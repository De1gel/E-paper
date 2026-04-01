#include "display/PartialRefresh.h"

#include "Display_EPD_W21.h"

namespace partial_refresh {
namespace {
constexpr uint8_t kCmdPartialWindow = 0x83;
}

void fillWindowSolid(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                     uint8_t color_nibble) {
  const uint16_t x_end = static_cast<uint16_t>(x + width - 1);
  const uint16_t y_end = static_cast<uint16_t>(y + height - 1);

  EPD_W21_WriteCMD(kCmdPartialWindow);
  EPD_W21_WriteDATA((x >> 8) & 0x03);
  EPD_W21_WriteDATA(x & 0xFF);
  EPD_W21_WriteDATA((x_end >> 8) & 0x03);
  EPD_W21_WriteDATA(x_end & 0xFF);
  EPD_W21_WriteDATA((y >> 8) & 0x03);
  EPD_W21_WriteDATA(y & 0xFF);
  EPD_W21_WriteDATA((y_end >> 8) & 0x03);
  EPD_W21_WriteDATA(y_end & 0xFF);
  EPD_W21_WriteDATA(0x01);
  lcd_chkstatus();

  EPD_W21_WriteCMD(DTM);
  const uint8_t packed =
      static_cast<uint8_t>(((color_nibble & 0x0F) << 4) | (color_nibble & 0x0F));

  for (uint16_t j = 0; j < height; ++j) {
    for (uint16_t i = 0; i < width; i += 2) {
      EPD_W21_WriteDATA(packed);
    }
    if ((j % 10U) == 0U) {
      yield();
    }
  }
  lcd_chkstatus();

  EPD_W21_WriteCMD(PON);
  lcd_chkstatus();
  EPD_W21_WriteCMD(BTST2);
  EPD_W21_WriteDATA(0x6F);
  EPD_W21_WriteDATA(0x1F);
  EPD_W21_WriteDATA(0x17);
  EPD_W21_WriteDATA(0x49);
  EPD_W21_WriteCMD(DRF);
  EPD_W21_WriteDATA(0x00);
  lcd_chkstatus();
  EPD_W21_WriteCMD(POF);
  EPD_W21_WriteDATA(0x00);
  lcd_chkstatus();
}

}  // namespace partial_refresh
