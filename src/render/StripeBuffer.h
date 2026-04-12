#ifndef STRIPE_BUFFER_H
#define STRIPE_BUFFER_H

#include <Arduino.h>

namespace render {

class StripeBuffer {
 public:
  ~StripeBuffer();

  bool ensure(uint16_t width_px, uint16_t rows);
  void clear(uint8_t color_nibble);

  uint8_t *data();
  const uint8_t *data() const;
  size_t sizeBytes() const;
  uint16_t rowBytes() const;
  uint16_t widthPx() const;
  uint16_t rows() const;
  bool ready() const;

 private:
  uint8_t *data_ = nullptr;
  uint16_t width_px_ = 0;
  uint16_t rows_ = 0;
};

}  // namespace render

#endif
