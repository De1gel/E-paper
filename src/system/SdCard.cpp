#include "system/SdCard.h"

#include <SD.h>
#include <SPI.h>

namespace appfw {
namespace {

constexpr uint8_t kSdCsPin = 5;
constexpr uint8_t kSdSckPin = 18;
constexpr uint8_t kSdMisoPin = 19;
constexpr uint8_t kSdMosiPin = 23;

SPIClass g_sd_spi(HSPI);
bool g_spi_started = false;
bool g_sd_mounted = false;

}  // namespace

bool mountSdCard(const char *reason) {
  if (g_sd_mounted) {
    return true;
  }
  if (!g_spi_started) {
    g_sd_spi.begin(kSdSckPin, kSdMisoPin, kSdMosiPin, kSdCsPin);
    g_spi_started = true;
  }
  g_sd_mounted = SD.begin(kSdCsPin, g_sd_spi);
  Serial.printf("[SD] mount %s reason=%s\n",
                g_sd_mounted ? "ok" : "failed",
                reason ? reason : "unknown");
  return g_sd_mounted;
}

void unmountSdCard(const char *reason) {
  if (!g_sd_mounted) {
    return;
  }
  SD.end();
  g_sd_mounted = false;
  Serial.printf("[SD] unmounted reason=%s\n", reason ? reason : "unknown");
}

bool isSdCardMounted() {
  return g_sd_mounted;
}

}  // namespace appfw

