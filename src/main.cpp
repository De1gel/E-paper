#include <Arduino.h>
#include <SPI.h>
#include <esp_bt.h>
#include <esp_log.h>
#include <esp_system.h>

#include "app/App.h"
#include "Display_EPD_W21.h"
#include "Display_EPD_W21_spi.h"
#include "system/LightSleepController.h"
#include "system/LogConfig.h"

#ifndef APP_GREEN_CALIBRATION_FIRMWARE
#define APP_GREEN_CALIBRATION_FIRMWARE 0
#endif
#ifndef APP_EPD_POST_REFRESH_SETTLE_MS
#define APP_EPD_POST_REFRESH_SETTLE_MS 0
#endif

#if APP_GREEN_CALIBRATION_FIRMWARE
namespace {
constexpr uint8_t kBayer4x4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

// Five-bit-wide glyphs for the B00..B15 cell labels.
constexpr uint8_t kGlyphs[11][7] = {
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},  // B
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},  // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},  // 1
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},  // 2
    {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},  // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},  // 4
    {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},  // 5
    {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E},  // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},  // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},  // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E},  // 9
};

uint8_t calibrationLabelPixel(uint8_t value, uint16_t local_x, uint16_t local_y) {
  if (local_x >= 48u || local_y >= 18u) {
    return 0xFFu;
  }
  if (local_x < 4u || local_y < 2u) {
    return white;
  }
  const uint16_t text_x = local_x - 4u;
  const uint16_t text_y = local_y - 2u;
  if (text_y >= 14u) {
    return white;
  }
  const uint8_t character = static_cast<uint8_t>(text_x / 12u);
  const uint8_t character_x = static_cast<uint8_t>(text_x % 12u);
  if (character >= 3u || character_x >= 10u) {
    return white;
  }
  const uint8_t glyph = (character == 0u)
                            ? 0u
                            : static_cast<uint8_t>(1u + ((character == 1u)
                                                            ? value / 10u
                                                            : value % 10u));
  const uint8_t row = static_cast<uint8_t>(text_y / 2u);
  const uint8_t column = static_cast<uint8_t>(character_x / 2u);
  return (kGlyphs[glyph][row] & (0x10u >> column)) ? black : white;
}

uint8_t calibrationPixel(uint16_t x, uint16_t y) {
  constexpr uint16_t kCellWidth = 200u;
  constexpr uint16_t kCellHeight = 120u;
  const uint8_t column = static_cast<uint8_t>(x / kCellWidth);
  const uint8_t row = static_cast<uint8_t>(y / kCellHeight);
  const uint8_t blue_count = static_cast<uint8_t>(row * 4u + column);
  const uint16_t local_x = static_cast<uint16_t>(x % kCellWidth);
  const uint16_t local_y = static_cast<uint16_t>(y % kCellHeight);
  const uint8_t label = calibrationLabelPixel(blue_count, local_x, local_y);
  if (label != 0xFFu) {
    return label;
  }
  return (kBayer4x4[y & 3u][x & 3u] < blue_count) ? blue : green;
}

void showGreenCalibrationCard() {
  Serial.println("[CALIBRATION] rendering B00..B15 green/blue card");
  EPD_init_fast();
  EPD_W21_WriteCMD(0x10);
  for (uint16_t y = 0; y < EPD_HEIGHT; ++y) {
    for (uint16_t x = 0; x < EPD_WIDTH; x += 2u) {
      const uint8_t high = calibrationPixel(x, y);
      const uint8_t low = calibrationPixel(static_cast<uint16_t>(x + 1u), y);
      EPD_W21_WriteDATA(static_cast<uint8_t>((high << 4u) | low));
    }
    if ((y & 0x1Fu) == 0u) {
      yield();
    }
  }
  EPD_W21_WriteCMD(0x12);
  EPD_W21_WriteDATA(0x00);
  delay(1);
  while (!isEPD_W21_BUSY) {
    delay(2);
  }
#if APP_EPD_POST_REFRESH_SETTLE_MS > 0
  delay(APP_EPD_POST_REFRESH_SETTLE_MS);
#endif
  EPD_sleep();
  digitalWrite(32, LOW);
  Serial.println("[CALIBRATION] done; report the most natural Bxx cell");
}
}  // namespace
#endif

App g_app;
appfw::LightSleepController g_light_sleep;
RTC_DATA_ATTR uint32_t g_rtc_boot_count = 0;

const char *resetReasonName(esp_reset_reason_t reason);

const char *sleepWakeCauseName(esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      return "UNDEFINED";
    case ESP_SLEEP_WAKEUP_EXT0:
      return "EXT0";
    case ESP_SLEEP_WAKEUP_EXT1:
      return "EXT1";
    case ESP_SLEEP_WAKEUP_TIMER:
      return "TIMER";
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      return "TOUCHPAD";
    case ESP_SLEEP_WAKEUP_ULP:
      return "ULP";
    case ESP_SLEEP_WAKEUP_GPIO:
      return "GPIO";
    case ESP_SLEEP_WAKEUP_UART:
      return "UART";
    default:
      return "OTHER";
  }
}

void logBootDiagnostics() {
  ++g_rtc_boot_count;
  const esp_reset_reason_t reset_reason = esp_reset_reason();
  const esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();
  Serial.printf("[DIAG][BOOT] rtc_boot_count=%lu reset=%s(%d) sleep_wake=%s(%d)\n",
                static_cast<unsigned long>(g_rtc_boot_count),
                resetReasonName(reset_reason),
                static_cast<int>(reset_reason),
                sleepWakeCauseName(wake_cause),
                static_cast<int>(wake_cause));
}

void disableUnusedBluetooth() {
  esp_bt_controller_status_t status = esp_bt_controller_get_status();
  if (status == ESP_BT_CONTROLLER_STATUS_ENABLED) {
    esp_bt_controller_disable();
    status = esp_bt_controller_get_status();
  }
  if (status == ESP_BT_CONTROLLER_STATUS_INITED) {
    esp_bt_controller_deinit();
  }
  Serial.println("[POWER] bluetooth controller disabled");
}

void configureRuntimeLogging() {
  Serial.setDebugOutput(appfw::kDebugLogs);
  esp_log_level_set("*", appfw::kDebugLogs ? ESP_LOG_INFO : ESP_LOG_WARN);
  Serial.printf("[LOG] mode=%s\n", appfw::kDebugLogs ? "debug" : "normal");
}

const char *resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_UNKNOWN:
      return "UNKNOWN";
    case ESP_RST_POWERON:
      return "POWERON";
    case ESP_RST_EXT:
      return "EXT";
    case ESP_RST_SW:
      return "SW";
    case ESP_RST_PANIC:
      return "PANIC";
    case ESP_RST_INT_WDT:
      return "INT_WDT";
    case ESP_RST_TASK_WDT:
      return "TASK_WDT";
    case ESP_RST_WDT:
      return "WDT";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT";
    case ESP_RST_SDIO:
      return "SDIO";
    default:
      return "INVALID";
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("[BOOT] setup begin");
  Serial.printf("[BOOT] reset reason=%s (%d)\n",
                resetReasonName(esp_reset_reason()),
                static_cast<int>(esp_reset_reason()));
  logBootDiagnostics();
  configureRuntimeLogging();
  disableUnusedBluetooth();

  pinMode(25, INPUT);   // BUSY
  pinMode(26, OUTPUT);  // RES
  pinMode(27, OUTPUT);  // DC
  pinMode(33, OUTPUT);  // CS
  pinMode(32, OUTPUT);  // PWR_ON

  digitalWrite(32, HIGH);
  Serial.println("[INIT] IO32 power enabled");

  SPI.end();
  SPI.begin(13, 12, 14, 33);
  Serial.println("[INIT] SPI configured: SCK=13 MISO=12 MOSI=14 CS=33");
  Serial.println("[BOOT] setup done");

#if APP_GREEN_CALIBRATION_FIRMWARE
  showGreenCalibrationCard();
#else
  g_app.begin();
#endif
}

void loop() {
#if APP_GREEN_CALIBRATION_FIRMWARE
  delay(1000);
#else
  uint32_t now_ms = millis();
  g_app.update(now_ms);
  g_app.render();
#if APP_ENABLE_LIGHT_SLEEP
  now_ms = millis();
  if (g_app.canEnterLightSleep(now_ms)) {
    const uint32_t deadline_ms = g_app.nextWakeDeadlineMs(now_ms);
    if (deadline_ms > now_ms + 200u) {
      g_app.onEnterLightSleep(deadline_ms);
      const appfw::LightSleepResult result =
          g_light_sleep.sleepUntil(now_ms, deadline_ms, g_app.shouldWakeFromSideKeys());
      if (result.slept) {
        g_app.onWakeFromLightSleep(static_cast<uint64_t>(result.slept_ms) * 1000ULL,
                                   result.wake_reason == appfw::LightSleepWakeReason::Gpio,
                                   result.wake_up_pressed,
                                   result.wake_mid_pressed,
                                   result.wake_down_pressed);
        return;
      }
      g_app.cancelLightSleepEntry("sleep_not_entered");
    }
  }
#endif
  delay(50);
#endif
}
