#include <Arduino.h>
#include <SPI.h>

#include "app/App.h"

App g_app;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("[BOOT] setup begin");

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
  const uint32_t now_ms = millis();
  g_app.update(now_ms);
  g_app.render();
  delay(50);
}
