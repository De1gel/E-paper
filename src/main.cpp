#include <Arduino.h>
#include <SPI.h>
#include <esp_system.h>

#include "app/App.h"
#include "system/LightSleepController.h"

App g_app;
appfw::LightSleepController g_light_sleep;

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

  g_app.begin();
}

void loop() {
  uint32_t now_ms = millis();
  g_app.update(now_ms);
  g_app.render();
  now_ms = millis();
  if (g_app.canEnterLightSleep(now_ms)) {
    const uint32_t deadline_ms = g_app.nextWakeDeadlineMs(now_ms);
    if (deadline_ms > now_ms + 200u) {
      g_app.onEnterLightSleep(deadline_ms);
      const appfw::LightSleepResult result = g_light_sleep.sleepUntil(now_ms, deadline_ms);
      if (result.slept) {
        g_app.onWakeFromLightSleep(static_cast<uint64_t>(result.slept_ms) * 1000ULL,
                                   result.wake_reason == appfw::LightSleepWakeReason::Gpio);
        return;
      }
      g_app.cancelLightSleepEntry("sleep_not_entered");
    }
  }
  delay(50);
}
