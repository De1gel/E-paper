#include <Arduino.h>
#include <SPI.h>
#include <esp_bt.h>
#include <esp_log.h>
#include <esp_system.h>

#include "app/App.h"
#include "system/LightSleepController.h"
#include "system/LogConfig.h"

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

  g_app.begin();
}

void loop() {
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
}
