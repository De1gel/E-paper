#include "display/PartialRefresh.h"

#include "Display_EPD_W21.h"
#include "display/PartialRefreshGeometry.h"

namespace partial_refresh {
namespace {
constexpr uint8_t kCmdPartialWindow = 0x83;

void triggerPartialRefresh() {
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
  // Partial-window writes on this panel expect one mode byte before pixel data.
  // Without it, the first pixel byte is consumed as the mode and the whole window
  // data stream shifts, which visibly skews rendered glyphs.
  EPD_W21_WriteDATA(0x00);
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

  triggerPartialRefresh();
}

void writeWindowFromBuffer(const uint8_t *packed_buffer, uint16_t buffer_width_px,
                           uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
  if (packed_buffer == nullptr || buffer_width_px == 0 || width == 0 || height == 0) {
    return;
  }
  if (x >= EPD_WIDTH || y >= EPD_HEIGHT) {
    return;
  }
  if (width > buffer_width_px) {
    width = buffer_width_px;
  }
  if (x + width > EPD_WIDTH) {
    width = static_cast<uint16_t>(EPD_WIDTH - x);
  }
  if (y + height > EPD_HEIGHT) {
    height = static_cast<uint16_t>(EPD_HEIGHT - y);
  }
  if (width == 0 || height == 0) {
    return;
  }
  normalizePartialWindow(EPD_WIDTH, x, width);
  if (width == 0) {
    return;
  }

  const uint16_t x_end = static_cast<uint16_t>(x + width - 1u);
  const uint16_t y_end = static_cast<uint16_t>(y + height - 1u);

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
  // Match the verified partial-refresh command chain from the panel sample code.
  EPD_W21_WriteDATA(0x00);
  const uint16_t src_stride_bytes = static_cast<uint16_t>(buffer_width_px / 2u);
  const uint16_t write_bytes_per_row = static_cast<uint16_t>(width / 2u);
  for (uint16_t row = 0; row < height; ++row) {
    const uint8_t *src = packed_buffer + static_cast<uint32_t>(row) * src_stride_bytes;
    for (uint16_t i = 0; i < write_bytes_per_row; ++i) {
      EPD_W21_WriteDATA(src[i]);
    }
    if ((row & 0x07u) == 0u) {
      yield();
    }
  }
  lcd_chkstatus();
  triggerPartialRefresh();
}

void writeWindowFromPacked(const uint8_t *packed_frame, uint16_t frame_width_px,
                           uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
  if (packed_frame == nullptr || frame_width_px == 0 || width == 0 || height == 0) {
    return;
  }
  if (x >= EPD_WIDTH || y >= EPD_HEIGHT) {
    return;
  }
  if (x + width > EPD_WIDTH) {
    width = static_cast<uint16_t>(EPD_WIDTH - x);
  }
  if (y + height > EPD_HEIGHT) {
    height = static_cast<uint16_t>(EPD_HEIGHT - y);
  }
  if (width == 0 || height == 0) {
    return;
  }
  normalizePartialWindow(EPD_WIDTH, x, width);
  if (width < kPartialWindowAlignPx) {
    return;
  }

  const uint16_t x_end = static_cast<uint16_t>(x + width - 1u);
  const uint16_t y_end = static_cast<uint16_t>(y + height - 1u);

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
  // Match the verified partial-refresh command chain from the panel sample code.
  EPD_W21_WriteDATA(0x00);
  const uint16_t src_stride_bytes = static_cast<uint16_t>(frame_width_px / 2u);
  const uint16_t write_bytes_per_row = static_cast<uint16_t>(width / 2u);
  for (uint16_t row = 0; row < height; ++row) {
    const uint32_t src_offset =
        static_cast<uint32_t>(static_cast<uint32_t>(y + row) * src_stride_bytes) + (x / 2u);
    const uint8_t *src = packed_frame + src_offset;
    for (uint16_t i = 0; i < write_bytes_per_row; ++i) {
      EPD_W21_WriteDATA(src[i]);
    }
    if ((row & 0x07u) == 0u) {
      yield();
    }
  }
  lcd_chkstatus();
  triggerPartialRefresh();
}

}  // namespace partial_refresh
