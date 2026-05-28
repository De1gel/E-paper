#include <Arduino.h>
#include <SPI.h>

namespace {
constexpr uint8_t kBusyPin = 25;
constexpr uint8_t kResetPin = 26;
constexpr uint8_t kDcPin = 27;
constexpr uint8_t kCsPin = 33;
constexpr uint8_t kPowerPin = 32;

constexpr uint16_t kScreenWidth = 800;
constexpr uint16_t kScreenHeight = 480;
constexpr uint8_t kGridSize = 8;
constexpr uint16_t kBlockWidth = kScreenWidth / kGridSize;
constexpr uint16_t kBlockHeight = kScreenHeight / kGridSize;

constexpr uint8_t kBlack = 0x00;
constexpr uint8_t kWhite = 0x01;
constexpr uint8_t kYellow = 0x02;
constexpr uint8_t kRed = 0x03;
constexpr uint8_t kBlue = 0x05;
constexpr uint8_t kGreen = 0x06;

constexpr uint8_t kPackedWhite = 0x11;

constexpr uint8_t PSR = 0x00;
constexpr uint8_t PWRR = 0x01;
constexpr uint8_t POF = 0x02;
constexpr uint8_t POFS = 0x03;
constexpr uint8_t PON = 0x04;
constexpr uint8_t BTST1 = 0x05;
constexpr uint8_t BTST2 = 0x06;
constexpr uint8_t BTST3 = 0x08;
constexpr uint8_t DTM = 0x10;
constexpr uint8_t DRF = 0x12;
constexpr uint8_t PLL = 0x30;
constexpr uint8_t CDI = 0x50;
constexpr uint8_t TCON = 0x60;
constexpr uint8_t TRES = 0x61;
constexpr uint8_t T_VDCS = 0x84;
constexpr uint8_t PWS = 0xE3;
constexpr uint8_t CMD_PARTIAL_WINDOW = 0x83;

uint8_t chessboard[kGridSize][kGridSize];
uint8_t partial_step = 0;

inline void epdCsLow() { digitalWrite(kCsPin, LOW); }
inline void epdCsHigh() { digitalWrite(kCsPin, HIGH); }
inline void epdDcCommand() { digitalWrite(kDcPin, LOW); }
inline void epdDcData() { digitalWrite(kDcPin, HIGH); }
inline void epdResetLow() { digitalWrite(kResetPin, LOW); }
inline void epdResetHigh() { digitalWrite(kResetPin, HIGH); }
inline bool epdReady() { return digitalRead(kBusyPin) != LOW; }

void spiWrite(uint8_t value) {
  SPI.transfer(value);
}

void epdWriteCommand(uint8_t command) {
  epdCsLow();
  epdDcCommand();
  spiWrite(command);
  epdCsHigh();
}

void epdWriteData(uint8_t data) {
  epdCsLow();
  epdDcData();
  spiWrite(data);
  epdCsHigh();
}

void lcdCheckStatus() {
  const uint32_t start_ms = millis();
  while (!epdReady()) {
    delay(2);
    if ((millis() - start_ms) > 120000UL) {
      Serial.println("[GDTEST] busy timeout");
      break;
    }
  }
  const uint32_t elapsed_ms = millis() - start_ms;
  if (elapsed_ms >= 200UL) {
    Serial.printf("[GDTEST] busy wait=%lums\n", static_cast<unsigned long>(elapsed_ms));
  }
}

void epdHardwareReset() {
  epdResetLow();
  delay(10);
  epdResetHigh();
  delay(10);
}

void epdInitGoodDisplay() {
  epdHardwareReset();

  epdWriteCommand(0xAA);
  epdWriteData(0x49);
  epdWriteData(0x55);
  epdWriteData(0x20);
  epdWriteData(0x08);
  epdWriteData(0x09);
  epdWriteData(0x18);

  epdWriteCommand(PWRR);
  epdWriteData(0x3F);

  epdWriteCommand(PSR);
  epdWriteData(0x5F);
  epdWriteData(0x69);

  epdWriteCommand(POFS);
  epdWriteData(0x00);
  epdWriteData(0x54);
  epdWriteData(0x00);
  epdWriteData(0x44);

  epdWriteCommand(BTST1);
  epdWriteData(0x40);
  epdWriteData(0x1F);
  epdWriteData(0x1F);
  epdWriteData(0x2C);

  epdWriteCommand(BTST2);
  epdWriteData(0x6F);
  epdWriteData(0x1F);
  epdWriteData(0x17);
  epdWriteData(0x49);

  epdWriteCommand(BTST3);
  epdWriteData(0x6F);
  epdWriteData(0x1F);
  epdWriteData(0x1F);
  epdWriteData(0x22);

  epdWriteCommand(PLL);
  epdWriteData(0x08);

  epdWriteCommand(CDI);
  epdWriteData(0x3F);

  epdWriteCommand(TCON);
  epdWriteData(0x02);
  epdWriteData(0x00);

  epdWriteCommand(TRES);
  epdWriteData(0x03);
  epdWriteData(0x20);
  epdWriteData(0x01);
  epdWriteData(0xE0);

  epdWriteCommand(T_VDCS);
  epdWriteData(0x01);

  epdWriteCommand(PWS);
  epdWriteData(0x2F);

  epdWriteCommand(PON);
  lcdCheckStatus();
}

void epdSleepGoodDisplay() {
  epdWriteCommand(POF);
  epdWriteData(0x00);
  lcdCheckStatus();
}

void triggerGoodDisplayRefresh() {
  epdWriteCommand(PON);
  lcdCheckStatus();

  epdWriteCommand(BTST2);
  epdWriteData(0x6F);
  epdWriteData(0x1F);
  epdWriteData(0x17);
  epdWriteData(0x49);

  epdWriteCommand(DRF);
  epdWriteData(0x00);
  lcdCheckStatus();

  epdWriteCommand(POF);
  epdWriteData(0x00);
  lcdCheckStatus();
}

void initChessboard() {
  for (uint8_t row = 0; row < kGridSize; ++row) {
    for (uint8_t col = 0; col < kGridSize; ++col) {
      chessboard[row][col] = ((row + col) & 0x01u) ? kBlack : kWhite;
    }
  }
}

void displayChessboard() {
  Serial.println("[GDTEST] full chessboard begin");
  epdWriteCommand(DTM);
  for (uint16_t y = 0; y < kScreenHeight; ++y) {
    const uint8_t grid_y = y / kBlockHeight;
    for (uint16_t x = 0; x < kScreenWidth; x += 2) {
      const uint8_t color1 = chessboard[grid_y][x / kBlockWidth];
      const uint8_t color2 = chessboard[grid_y][(x + 1u) / kBlockWidth];
      epdWriteData(static_cast<uint8_t>((color1 << 4) | (color2 & 0x0F)));
    }
    if ((y % 100u) == 0u) {
      yield();
    }
  }
  triggerGoodDisplayRefresh();
  Serial.println("[GDTEST] full chessboard done");
}

void clearWhite() {
  Serial.println("[GDTEST] clear white begin");
  epdWriteCommand(DTM);
  for (uint32_t i = 0; i < 192000UL; ++i) {
    epdWriteData(kPackedWhite);
    if ((i & 0x3FFFu) == 0u) {
      yield();
    }
  }
  epdWriteCommand(DRF);
  epdWriteData(0x00);
  delay(1);
  lcdCheckStatus();
  Serial.println("[GDTEST] clear white done");
}

void epdPartialWindow(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t color) {
  const uint16_t x_end = static_cast<uint16_t>(x + width - 1u);
  const uint16_t y_end = static_cast<uint16_t>(y + height - 1u);

  epdWriteCommand(CMD_PARTIAL_WINDOW);
  epdWriteData((x >> 8) & 0x03);
  epdWriteData(x & 0xFF);
  epdWriteData((x_end >> 8) & 0x03);
  epdWriteData(x_end & 0xFF);
  epdWriteData((y >> 8) & 0x03);
  epdWriteData(y & 0xFF);
  epdWriteData((y_end >> 8) & 0x03);
  epdWriteData(y_end & 0xFF);
  epdWriteData(0x01);
  lcdCheckStatus();

  epdWriteCommand(DTM);
  epdWriteData(0x00);
  const uint8_t packed = static_cast<uint8_t>((color << 4) | (color & 0x0F));
  for (uint16_t row = 0; row < height; ++row) {
    for (uint16_t col = 0; col < width; col += 2) {
      epdWriteData(packed);
    }
    if ((row % 10u) == 0u) {
      yield();
    }
  }
  lcdCheckStatus();
  triggerGoodDisplayRefresh();
}

void manualPartialUpdate(uint8_t grid_x, uint8_t grid_y, uint8_t color) {
  chessboard[grid_y][grid_x] = color;
  const uint16_t x = static_cast<uint16_t>(grid_x) * kBlockWidth;
  const uint16_t y = static_cast<uint16_t>(grid_y) * kBlockHeight;
  Serial.printf("[GDTEST] partial grid=(%u,%u) color=0x%02X begin\n", grid_x, grid_y, color);
  epdPartialWindow(x, y, kBlockWidth, kBlockHeight, color);
  Serial.println("[GDTEST] partial done");
}

void runPartialStep() {
  static constexpr uint8_t xs[] = {2, 4, 5, 1, 6, 3};
  static constexpr uint8_t ys[] = {2, 4, 1, 5, 6, 3};
  static constexpr uint8_t colors[] = {kRed, kYellow, kBlue, kGreen, kWhite, kBlack};
  const uint8_t idx = partial_step % (sizeof(colors) / sizeof(colors[0]));
  epdInitGoodDisplay();
  manualPartialUpdate(xs[idx], ys[idx], colors[idx]);
  epdSleepGoodDisplay();
  ++partial_step;
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("[GDTEST] GoodDisplay GDEP073E01 partial test");

  pinMode(kPowerPin, OUTPUT);
  digitalWrite(kPowerPin, HIGH);
  delay(20);

  pinMode(kBusyPin, INPUT);
  pinMode(kResetPin, OUTPUT);
  pinMode(kDcPin, OUTPUT);
  pinMode(kCsPin, OUTPUT);
  epdCsHigh();

  SPI.begin(18, -1, 23, kCsPin);
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));

  initChessboard();
  epdInitGoodDisplay();
  displayChessboard();
  epdSleepGoodDisplay();
}

void loop() {
  delay(5000);
  runPartialStep();

  if (partial_step > 0 && (partial_step % 12u) == 0u) {
    Serial.println("[GDTEST] rebuild full chessboard");
    initChessboard();
    epdInitGoodDisplay();
    displayChessboard();
    epdSleepGoodDisplay();
  }
}
