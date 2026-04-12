#include "render/StripeBuffer.h"

#include <esp_heap_caps.h>
#include <string.h>

namespace render {

StripeBuffer::~StripeBuffer() {
  if (data_ != nullptr) {
    free(data_);
  }
}

bool StripeBuffer::ensure(uint16_t width_px, uint16_t rows) {
  if (width_px == 0 || rows == 0) {
    return false;
  }
  if (data_ != nullptr && width_px_ == width_px && rows_ == rows) {
    return true;
  }

  if (data_ != nullptr) {
    free(data_);
    data_ = nullptr;
  }

  const size_t bytes = static_cast<size_t>(width_px / 2u) * rows;
  data_ = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
  if (data_ == nullptr) {
    width_px_ = 0;
    rows_ = 0;
    return false;
  }
  width_px_ = width_px;
  rows_ = rows;
  return true;
}

void StripeBuffer::clear(uint8_t color_nibble) {
  if (data_ == nullptr) {
    return;
  }
  const uint8_t packed =
      static_cast<uint8_t>(((color_nibble & 0x0Fu) << 4) | (color_nibble & 0x0Fu));
  memset(data_, packed, sizeBytes());
}

uint8_t *StripeBuffer::data() {
  return data_;
}

const uint8_t *StripeBuffer::data() const {
  return data_;
}

size_t StripeBuffer::sizeBytes() const {
  return static_cast<size_t>(width_px_ / 2u) * rows_;
}

uint16_t StripeBuffer::rowBytes() const {
  return static_cast<uint16_t>(width_px_ / 2u);
}

uint16_t StripeBuffer::widthPx() const {
  return width_px_;
}

uint16_t StripeBuffer::rows() const {
  return rows_;
}

bool StripeBuffer::ready() const {
  return data_ != nullptr;
}

}  // namespace render
