#include "system/WifiManager.h"

#include <FS.h>
#include <SD.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_adc_cal.h>

namespace appfw {
namespace {
constexpr const char *kDefaultApSsid = "PhotoFrame_Config";
constexpr const char *kDefaultApPass = "12345678";
constexpr const char *kDefaultStaSsid = "YOUR_WIFI_SSID";
constexpr const char *kDefaultStaPass = "YOUR_WIFI_PASSWORD";
constexpr const char *kDefaultTimezone = "Asia/Shanghai";
constexpr const char *kDefaultWeatherUrl = "http://192.168.4.1/api/weather";
constexpr const char *kPortalHtmlPath = "/index.html";
constexpr uint16_t kHttpPort = 80;

String jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); ++i) {
    const char c = s[i];
    if (c == '\\') {
      out += "\\\\";
    } else if (c == '"') {
      out += "\\\"";
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else {
      out += c;
    }
  }
  return out;
}

}  // namespace

void WifiManager::begin() {
  pinMode(kPeripheralPowerPin, OUTPUT);
  digitalWrite(kPeripheralPowerPin, LOW);
  initSensors();
  loadSettings();
  stop("boot");
}

void WifiManager::update(uint32_t now_ms) {
  updateSensors(now_ms);

  if (server_ != nullptr && (state_ == State::ApRunning || state_ == State::StaRunning)) {
    server_->handleClient();
  }

  if (state_ == State::StaConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      state_ = State::StaRunning;
      markActivity(now_ms);
      startServer();
      initSD();
      Serial.printf("[WIFI] STA connected ip=%s\n", WiFi.localIP().toString().c_str());
    } else if ((now_ms - sta_connect_start_ms_) >= kStaConnectTimeoutMs) {
      Serial.println("[WIFI] STA connect timeout -> stop");
      stop("sta_connect_timeout");
      auto_exit_requested_ = true;
    }
    return;
  }

  if (state_ == State::StaRunning && WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] STA lost connection -> stop");
    stop("sta_lost_connection");
    auto_exit_requested_ = true;
    return;
  }

  updateTimeout(now_ms);
}

void WifiManager::startAP() {
  stop("switch_to_ap");
  digitalWrite(kPeripheralPowerPin, HIGH);
  delay(3);
  WiFi.mode(WIFI_AP);
  const bool ok = WiFi.softAP(kDefaultApSsid, kDefaultApPass);
  if (!ok) {
    Serial.println("[WIFI] AP start failed");
    state_ = State::Idle;
    return;
  }

  state_ = State::ApRunning;
  markActivity(millis());
  startServer();
  initSD();
  Serial.printf("[WIFI] AP started ssid=%s ip=%s timeout=%lus\n", kDefaultApSsid,
                WiFi.softAPIP().toString().c_str(),
                static_cast<unsigned long>(kApIdleTimeoutMs / 1000));
  Serial.println("[WIFI] portal url: http://192.168.4.1/");
}

void WifiManager::startSTA() {
  stop("switch_to_sta");
  digitalWrite(kPeripheralPowerPin, HIGH);
  delay(3);
  WiFi.mode(WIFI_STA);
  WiFi.begin(settings_.sta_ssid.c_str(), settings_.sta_pass.c_str());
  sta_connect_start_ms_ = millis();
  state_ = State::StaConnecting;
  Serial.printf("[WIFI] STA connecting ssid=%s timeout=%lus\n", settings_.sta_ssid.c_str(),
                static_cast<unsigned long>(kStaConnectTimeoutMs / 1000));
}

void WifiManager::stop(const char *reason) {
  if (state_ != State::Idle) {
    Serial.printf("[WIFI] stop reason=%s\n", reason ? reason : "none");
  }
  stopServer();
  deinitSD();
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  digitalWrite(kPeripheralPowerPin, LOW);
  state_ = State::Idle;
  sta_connect_start_ms_ = 0;
  last_activity_ms_ = 0;
}

bool WifiManager::consumeAutoExitRequested() {
  const bool value = auto_exit_requested_;
  auto_exit_requested_ = false;
  return value;
}

const WifiManager::Settings &WifiManager::settings() const {
  return settings_;
}

void WifiManager::loadSettings() {
  if (prefs_ == nullptr) {
    prefs_ = new Preferences();
  }
  if (!prefs_->begin("config", false)) {
    applyDefaultSettings();
    return;
  }

  applyDefaultSettings();
  if (prefs_->isKey("sta_ssid")) settings_.sta_ssid = prefs_->getString("sta_ssid", kDefaultStaSsid);
  if (prefs_->isKey("sta_pass")) settings_.sta_pass = prefs_->getString("sta_pass", kDefaultStaPass);
  if (prefs_->isKey("timezone")) settings_.timezone = prefs_->getString("timezone", kDefaultTimezone);
  if (prefs_->isKey("photo_sec")) settings_.photo_interval_sec = prefs_->getUInt("photo_sec", 3600);
  if (prefs_->isKey("weather_url")) settings_.weather_url = prefs_->getString("weather_url", kDefaultWeatherUrl);
  prefs_->end();
}

void WifiManager::saveSettings() {
  if (prefs_ == nullptr) {
    prefs_ = new Preferences();
  }
  if (!prefs_->begin("config", false)) {
    Serial.println("[CFG] preferences open failed");
    return;
  }
  prefs_->putString("sta_ssid", settings_.sta_ssid);
  prefs_->putString("sta_pass", settings_.sta_pass);
  prefs_->putString("timezone", settings_.timezone);
  prefs_->putUInt("photo_sec", settings_.photo_interval_sec);
  prefs_->putString("weather_url", settings_.weather_url);
  prefs_->end();
  Serial.println("[CFG] settings saved");
}

void WifiManager::applyDefaultSettings() {
  settings_.sta_ssid = kDefaultStaSsid;
  settings_.sta_pass = kDefaultStaPass;
  settings_.timezone = kDefaultTimezone;
  settings_.photo_interval_sec = 3600;
  settings_.weather_url = kDefaultWeatherUrl;
}

void WifiManager::startServer() {
  stopServer();
  server_ = new WebServer(kHttpPort);

  server_->on("/", HTTP_GET, [this]() { handleRoot(); });
  server_->on("/favicon.ico", HTTP_ANY, [this]() {
    markActivity(millis());
    server_->send(204, "text/plain", "");
  });
  server_->on("/api/status", HTTP_GET, [this]() { handleStatus(); });
  server_->on("/api/settings", HTTP_GET, [this]() { handleSettingsGet(); });
  server_->on("/api/settings", HTTP_POST, [this]() { handleSettingsPost(); });
  server_->on("/api/files", HTTP_GET, [this]() { handleFilesList(); });
  server_->on("/api/dir", HTTP_POST, [this]() { handleDirCreate(); });
  server_->on("/api/file", HTTP_GET, [this]() { handleFileDownload(); });
  server_->on("/api/file", HTTP_DELETE, [this]() { handleFileDelete(); });
  server_->on("/api/weather_test", HTTP_GET, [this]() { handleWeatherTest(); });
  server_->on("/api/stop", HTTP_POST, [this]() { handleStopPortal(); });
  server_->on(
      "/api/upload", HTTP_POST,
      [this]() {
        markActivity(millis());
        if (upload_ok_) {
          server_->send(200, "application/json", "{\"ok\":true}");
          return;
        }
        String json = "{\"ok\":false,\"error\":\"";
        json += jsonEscape(upload_error_);
        json += "\"}";
        server_->send(500, "application/json", json);
      },
      [this]() { handleFileUpload(); });
  server_->onNotFound([this]() { handleNotFound(); });
  server_->begin();
  Serial.println("[WIFI] web server started");
}

void WifiManager::stopServer() {
  if (server_ == nullptr) {
    return;
  }
  server_->stop();
  delete server_;
  server_ = nullptr;
}

void WifiManager::updateTimeout(uint32_t now_ms) {
  if (state_ == State::ApRunning) {
    if (last_activity_ms_ == 0) {
      markActivity(now_ms);
      return;
    }
    // Keep AP alive while at least one station is connected.
    const int sta_num = WiFi.softAPgetStationNum();
    if (sta_num > 0) {
      markActivity(now_ms);
    }
    const uint32_t idle_ms = now_ms - last_activity_ms_;
    if (idle_ms >= kApIdleTimeoutMs) {
      Serial.printf("[WIFI] AP idle timeout -> stop (idle_ms=%lu, sta_num=%d)\n",
                    static_cast<unsigned long>(idle_ms), sta_num);
      stop("ap_idle_timeout");
      auto_exit_requested_ = true;
    }
    return;
  }
  if (state_ == State::StaRunning) {
    if ((now_ms - last_activity_ms_) >= kStaSessionTimeoutMs) {
      Serial.println("[WIFI] STA idle timeout -> stop");
      stop("sta_idle_timeout");
      auto_exit_requested_ = true;
    }
  }
}

void WifiManager::markActivity(uint32_t now_ms) {
  last_activity_ms_ = now_ms;
}

void WifiManager::initSD() {
  if (sd_ready_) {
    return;
  }
  if (!sd_spi_started_) {
    sd_spi_.begin(kSdSckPin, kSdMisoPin, kSdMosiPin, kSdCsPin);
    sd_spi_started_ = true;
  }
  sd_ready_ = SD.begin(kSdCsPin, sd_spi_);
  Serial.printf("[SD] init %s\n", sd_ready_ ? "ok" : "failed");
}

void WifiManager::deinitSD() {
  if (sd_ready_) {
    SD.end();
    sd_ready_ = false;
  }
}

void WifiManager::initWebFs() {
  if (web_fs_ready_) {
    return;
  }
  web_fs_ready_ = SPIFFS.begin(false);
  Serial.printf("[WEB] SPIFFS init %s\n", web_fs_ready_ ? "ok" : "failed");
}

String WifiManager::listDirectoryJson(const char *path) {
  if (!sd_ready_) {
    return "{\"ok\":false,\"error\":\"sd_not_ready\"}";
  }

  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) {
    return "{\"ok\":false,\"error\":\"invalid_path\"}";
  }

  String out = "{\"ok\":true,\"path\":\"";
  out += jsonEscape(path);
  out += "\",\"items\":[";
  uint32_t skipped = 0;
  bool first = true;
  File entry = dir.openNextFile();
  while (entry) {
    const String name = String(entry.name());
    if (String(path) == "/pic" && !entry.isDirectory() && !isEpd4Path(name)) {
      ++skipped;
      entry = dir.openNextFile();
      continue;
    }
    if (!first) {
      out += ",";
    }
    first = false;
    out += "{\"name\":\"";
    out += jsonEscape(name);
    out += "\",\"dir\":";
    out += entry.isDirectory() ? "true" : "false";
    out += ",\"size\":";
    out += String(static_cast<uint32_t>(entry.size()));
    out += "}";
    entry = dir.openNextFile();
  }
  out += "],\"skipped\":";
  out += String(skipped);
  out += "}";
  Serial.printf("[SD] list path=%s items_done skipped=%lu\n", path,
                static_cast<unsigned long>(skipped));
  return out;
}

String WifiManager::contentTypeForPath(const String &path) const {
  if (path.endsWith(".html")) return "text/html";
  if (path.endsWith(".css")) return "text/css";
  if (path.endsWith(".js")) return "application/javascript";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".bmp")) return "image/bmp";
  return "application/octet-stream";
}

bool WifiManager::isSafePath(const String &path) const {
  if (path.length() == 0 || path[0] != '/') {
    return false;
  }
  if (path.indexOf("..") >= 0 || path.indexOf('\\') >= 0) {
    return false;
  }
  return true;
}

bool WifiManager::isEpd4Path(const String &path) const {
  String lower = path;
  lower.toLowerCase();
  return lower.endsWith(".epd4");
}

String WifiManager::currentIp() const {
  if (state_ == State::ApRunning) {
    return WiFi.softAPIP().toString();
  }
  return WiFi.localIP().toString();
}

void WifiManager::initSensors() {
  battery_adc_pin_ = 39;  // User-requested trial pin.
  pinMode(battery_adc_pin_, INPUT);
  analogSetPinAttenuation(static_cast<uint8_t>(battery_adc_pin_), ADC_11db);
  analogReadResolution(12);
  battery_mv_ = readBatteryMilliVolts(battery_adc_pin_);
  last_sensor_poll_ms_ = millis();
  Serial.printf("[SENSOR] battery adc pin=%d mv=%d\n", battery_adc_pin_, battery_mv_);
}

void WifiManager::updateSensors(uint32_t now_ms) {
  if ((now_ms - last_sensor_poll_ms_) < kSensorPollIntervalMs) {
    return;
  }
  last_sensor_poll_ms_ = now_ms;
  if (battery_adc_pin_ >= 0) {
    battery_mv_ = readBatteryMilliVolts(battery_adc_pin_);
  }
}

bool WifiManager::readAHT20(float &temperature_c, float &humidity_pct) {
  (void)temperature_c;
  (void)humidity_pct;
  return false;
}

int WifiManager::readBatteryMilliVolts(int pin) const {
  const int mv = analogReadMilliVolts(static_cast<uint8_t>(pin));
  return mv;
}

void WifiManager::detectBatteryPin() {}

float WifiManager::estimateBatteryPercent(int battery_mv) const {
  if (battery_mv <= 0) {
    return -1.0f;
  }
  constexpr float kMinMv = 3300.0f;
  constexpr float kMaxMv = 4200.0f;
  float pct = (static_cast<float>(battery_mv) - kMinMv) * 100.0f / (kMaxMv - kMinMv);
  if (pct < 0.0f) pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;
  return pct;
}

void WifiManager::handleRoot() {
  markActivity(millis());
  const String mode = (state_ == State::ApRunning) ? "AP" : ((state_ == State::StaRunning) ? "STA" : "IDLE");
  Serial.printf("[HTTP] GET / from %s mode=%s\n", server_->client().remoteIP().toString().c_str(),
                mode.c_str());
  initWebFs();
  if (web_fs_ready_) {
    File file = SPIFFS.open(kPortalHtmlPath, FILE_READ);
    if (file) {
      server_->streamFile(file, "text/html; charset=utf-8");
      file.close();
      return;
    }
    Serial.println("[WEB] missing /index.html in SPIFFS, falling back to embedded page");
  } else {
    Serial.println("[WEB] SPIFFS not ready, falling back to embedded page");
  }
  String html;
  html.reserve(18000);
  html += R"HTML(
<!doctype html>
<html>
<head>
  <meta charset='utf-8'>
  <meta name='viewport' content='width=device-width,initial-scale=1'>
  <title>E-paper 管理台</title>
  <style>
    :root {
      --bg1:#eef3f8;
      --bg2:#f7f9fc;
      --card:#ffffffd9;
      --line:#d7dee7;
      --text:#1f2a36;
      --muted:#5d6b79;
      --accent:#0f7ae5;
      --accent2:#2aa3f7;
      --danger:#df4d4d;
      --shadow:0 10px 24px rgba(17,34,68,.08);
      --soft:#eef5fc;
    }
    * { box-sizing: border-box; }
    body {
      margin:0;
      font-family:"Noto Sans SC","Source Han Sans SC","PingFang SC","Microsoft YaHei",sans-serif;
      background:
        radial-gradient(1200px 500px at -10% -20%, #d5e7ff 0%, transparent 60%),
        radial-gradient(1000px 600px at 110% -10%, #d8f1ff 0%, transparent 60%),
        linear-gradient(180deg,var(--bg1),var(--bg2));
      color:var(--text);
      min-height:100vh;
    }
    .wrap { max-width: 980px; margin: 20px auto; padding: 0 12px; }
    .top {
      background:var(--card);
      border:1px solid var(--line);
      border-radius:14px;
      padding:14px;
      margin-bottom:12px;
      backdrop-filter: blur(3px);
      box-shadow: var(--shadow);
    }
    .title { font-size:24px; font-weight:700; margin-bottom:6px; letter-spacing:.3px; }
    .meta { color:var(--muted); font-size:14px; }
    .tabs { display:flex; gap:8px; margin-top:10px; }
    .tab-btn {
      border:1px solid var(--line);
      background:#fff;
      color:var(--text);
      padding:8px 13px;
      border-radius:10px;
      cursor:pointer;
      transition:.2s ease;
    }
    .tab-btn:hover { transform: translateY(-1px); border-color:#b9c7d6; }
    .tab-btn.active {
      background: linear-gradient(135deg,var(--accent),var(--accent2));
      color:#fff;
      border-color:transparent;
      box-shadow: 0 4px 12px rgba(15,122,229,.28);
    }
    .card {
      background:var(--card);
      border:1px solid var(--line);
      border-radius:14px;
      padding:14px;
      margin-bottom:12px;
      backdrop-filter: blur(3px);
      box-shadow: var(--shadow);
    }
    .tab-panel { display:none; }
    .tab-panel.active { display:block; }
    .grid2 { display:grid; grid-template-columns:1fr 1fr; gap:10px; }
    .field label { display:block; font-size:13px; color:var(--muted); margin-bottom:4px; }
    .field input {
      width:100%;
      padding:9px 10px;
      border:1px solid var(--line);
      border-radius:10px;
      background:#fff;
      transition:.2s ease;
    }
    .field input:focus { outline:none; border-color:var(--accent); box-shadow:0 0 0 3px rgba(15,122,229,.12); }
    .btn {
      padding:8px 12px;
      border:1px solid var(--line);
      border-radius:10px;
      background:#fff;
      cursor:pointer;
      transition:.2s ease;
    }
    .btn:hover { transform: translateY(-1px); }
    .btn.primary { background: linear-gradient(135deg,var(--accent),var(--accent2)); color:#fff; border-color:transparent; }
    .btn.warn { background: var(--danger); color:#fff; border-color:var(--danger); }
    .row { display:flex; gap:8px; align-items:center; flex-wrap:wrap; }
    pre {
      margin:0;
      padding:10px;
      background:#f5f7fa;
      border:1px solid var(--line);
      border-radius:10px;
      overflow:auto;
      line-height:1.45;
    }
    table { width:100%; border-collapse:collapse; margin-top:8px; }
    th,td { padding:8px; border-bottom:1px solid var(--line); text-align:left; font-size:14px; }
    th { color:var(--muted); font-weight:600; }
    .small { font-size:12px; color:#6b7785; }
    .status-grid { display:grid; grid-template-columns:repeat(2,minmax(220px,1fr)); gap:10px; margin-top:10px; }
    .status-item { border:1px solid var(--line); border-radius:10px; background:#f7f9fc; padding:10px; }
    .status-item .k { font-size:12px; color:var(--muted); margin-bottom:4px; }
    .status-item .v { font-size:16px; font-weight:600; color:var(--text); }
    .notice {
      margin-top:10px;
      padding:10px 12px;
      border:1px solid #cfe0f3;
      background:#f3f8fe;
      border-radius:10px;
      color:#37516d;
      white-space:pre-wrap;
      display:none;
    }
    .path-bar {
      display:flex;
      gap:8px;
      align-items:center;
      justify-content:space-between;
      flex-wrap:wrap;
      margin-bottom:10px;
    }
    .path-tools { display:flex; gap:8px; align-items:center; flex-wrap:wrap; }
    .crumbs { display:flex; gap:6px; align-items:center; flex-wrap:wrap; margin-bottom:10px; }
    .crumb {
      border:1px solid var(--line);
      background:var(--soft);
      color:var(--text);
      padding:6px 10px;
      border-radius:999px;
      cursor:pointer;
    }
    .crumb.current { background:#fff; font-weight:600; }
    .quick-links { display:flex; gap:8px; flex-wrap:wrap; margin-bottom:10px; }
    .file-table td.actions { width:220px; }
    .name-btn {
      border:none;
      background:none;
      color:var(--text);
      cursor:pointer;
      padding:0;
      font:inherit;
      text-align:left;
    }
    .name-btn.dir { color:var(--accent); font-weight:600; }
    .badge {
      display:inline-block;
      min-width:56px;
      padding:4px 8px;
      border-radius:999px;
      background:#eef4fb;
      color:#46627f;
      font-size:12px;
      text-align:center;
    }
    .badge.dir { background:#dff0ff; color:#185b91; }
    .upload-extra {
      margin-top:10px;
      padding:12px;
      border:1px solid var(--line);
      border-radius:12px;
      background:#f8fbff;
      display:none;
    }
    .upload-extra.show { display:block; }
    .hint-line { margin-top:8px; font-size:12px; color:#617180; line-height:1.6; }
    .tooltip {
      position:relative;
      display:inline-flex;
      align-items:center;
      justify-content:center;
      width:18px;
      height:18px;
      border-radius:50%;
      border:1px solid #b9c7d6;
      color:#4c6075;
      font-size:12px;
      cursor:help;
      background:#fff;
    }
    .tooltip .tip {
      position:absolute;
      left:50%;
      bottom:calc(100% + 8px);
      transform:translateX(-50%);
      width:min(320px, 80vw);
      padding:10px 12px;
      border-radius:10px;
      background:#1f2a36;
      color:#fff;
      font-size:12px;
      line-height:1.6;
      box-shadow:0 10px 24px rgba(17,34,68,.22);
      opacity:0;
      pointer-events:none;
      transition:.18s ease;
      white-space:normal;
      z-index:10;
    }
    .tooltip:hover .tip { opacity:1; }
    .range-wrap { display:flex; align-items:center; gap:8px; flex-wrap:wrap; }
    .range-wrap input[type='range'] { width:200px; }
    .mode-note {
      margin-top:8px;
      padding:8px 10px;
      border-radius:10px;
      background:#fff;
      border:1px dashed #c9d7e6;
      color:#5b6a78;
      font-size:12px;
      line-height:1.5;
    }
    @media (max-width: 700px) {
      .grid2 { grid-template-columns:1fr; }
      .status-grid { grid-template-columns:1fr; }
      .file-table td.actions { width:auto; }
    }
  </style>
</head>
<body>
  <div class='wrap'>
    <div class='top'>
      <div class='title'>E-paper 管理台</div>
      <div class='meta'>默认首页：设备状态</div>
      <div class='tabs'>
        <button class='tab-btn active' data-tab='status'>设备状态</button>
        <button class='tab-btn' data-tab='config'>配置</button>
        <button class='tab-btn' data-tab='files'>目录</button>
      </div>
    </div>

    <section id='tab-status' class='tab-panel active'>
      <div class='card'>
        <div class='row'>
          <button class='btn primary' onclick='loadStatus()'>刷新状态</button>
          <button class='btn warn' onclick='stopPortal()'>停止 WiFi 门户</button>
        </div>
        <div class='small' style='margin-top:8px'>当前设备状态</div>
        <div class='status-grid'>
          <div class='status-item'><div class='k'>运行模式</div><div class='v' id='st_mode'>--</div></div>
          <div class='status-item'><div class='k'>设备 IP</div><div class='v' id='st_ip'>--</div></div>
          <div class='status-item'><div class='k'>AP IP</div><div class='v' id='st_apip'>--</div></div>
          <div class='status-item'><div class='k'>SD 卡状态</div><div class='v' id='st_sd'>--</div></div>
          <div class='status-item'><div class='k'>已运行时长</div><div class='v' id='st_uptime'>--</div></div>
          <div class='status-item'><div class='k'>超时剩余</div><div class='v' id='st_timeout'>--</div></div>
          <div class='status-item'><div class='k'>温度</div><div class='v' id='st_temp'>未接入</div></div>
          <div class='status-item'><div class='k'>电量</div><div class='v' id='st_battery'>未接入</div></div>
        </div>
        <div id='statusNote' class='notice'></div>
      </div>
    </section>

    <section id='tab-config' class='tab-panel'>
      <div class='card'>
        <div class='grid2'>
          <div class='field'><label>STA SSID</label><input id='ssid'></div>
          <div class='field'><label>STA 密码</label><input id='pass' type='password'></div>
          <div class='field'><label>时区</label><input id='tz'></div>
          <div class='field'><label>轮播间隔（秒）</label><input id='sec' type='number' min='30'></div>
          <div class='field' style='grid-column:1/3'><label>天气接口 URL</label><input id='wurl'></div>
        </div>
        <div class='row' style='margin-top:10px'>
          <button class='btn primary' onclick='saveCfg()'>保存配置</button>
          <button class='btn' onclick='testWeather()'>测试天气服务</button>
        </div>
        <pre id='cfgBox' style='margin-top:8px'>等待操作...</pre>
      </div>
    </section>

    <section id='tab-files' class='tab-panel'>
      <div class='card'>
        <div class='path-bar'>
          <div class='path-tools'>
            <button class='btn' onclick='goRoot()'>根目录</button>
            <button class='btn' onclick='goUpDir()'>上一级</button>
            <button class='btn primary' onclick='listFiles()'>刷新目录</button>
          </div>
          <div class='small'>双击目录名或点击进入按钮即可浏览下一层</div>
        </div>
        <div id='crumbs' class='crumbs'></div>
        <div class='quick-links'>
          <button class='btn' onclick='openDir("/pic")'>图片目录 /pic</button>
          <button class='btn' onclick='openDir("/")'>存储根目录 /</button>
        </div>
        <table class='file-table'>
          <thead><tr><th>名称</th><th>大小</th><th>类型</th><th>操作</th></tr></thead>
          <tbody id='fileRows'></tbody>
        </table>
        <div class='small' id='fileSummary' style='margin-top:8px'>等待加载...</div>
      </div>
      <div class='card'>
        <div class='row'>
          <input id='uploadFile' type='file'>
          <select id='uploadMode' style='padding:8px;border:1px solid var(--line);border-radius:8px;min-width:220px;'>
            <option value='normal'>普通文件上传</option>
            <option value='fit'>图片缩放上传</option>
            <option value='crop'>图片裁剪上传</option>
          </select>
          <button class='btn primary' onclick='uploadFile()'>执行上传</button>
        </div>
        <div id='uploadExtra' class='upload-extra'>
          <div class='grid2'>
            <div class='field'>
              <label>抖动算法</label>
              <select id='ditherMode' style='padding:8px;border:1px solid var(--line);border-radius:8px;'>
                <option value='fs_serpentine' selected>Floyd-Steinberg（通用 / 人像推荐）</option>
                <option value='atkinson'>Atkinson（插画 / 图标推荐）</option>
                <option value='jjn'>Jarvis-Judice-Ninke（风景照推荐）</option>
              </select>
              <div id='ditherHint' class='hint-line'></div>
            </div>
            <div class='field'>
              <label style='display:flex;align-items:center;gap:6px;'>
                Gamma
                <span class='tooltip'>?
                  <span class='tip'>推荐值：1.00 为默认通用值；0.90-0.98 适合偏暗照片，可提亮暗部；1.05-1.15 适合风景或高亮场景，层次更稳；高于 1.20 会让画面更重、更深，但暗部细节更容易丢失；低于 0.90 会整体更亮，但可能发灰、对比下降。</span>
                </span>
              </label>
              <div class='range-wrap'>
                <input id='gammaCtrl' type='range' min='0.80' max='1.40' step='0.01' value='1.00'>
                <span id='gammaVal'>1.00</span>
              </div>
              <div id='gammaHint' class='hint-line'></div>
            </div>
          </div>
          <div id='modeNote' class='mode-note'></div>
        </div>
        <pre id='uploadBox' style='margin-top:8px'>等待上传...</pre>
      </div>
    </section>
  </div>

  <script>
    const tabs = [...document.querySelectorAll('.tab-btn')];
    const panels = {
      status: document.getElementById('tab-status'),
      config: document.getElementById('tab-config'),
      files: document.getElementById('tab-files'),
    };
    tabs.forEach(btn => btn.addEventListener('click', () => {
      tabs.forEach(b => b.classList.remove('active'));
      Object.values(panels).forEach(p => p.classList.remove('active'));
      btn.classList.add('active');
      panels[btn.dataset.tab].classList.add('active');
    }));
    const statusNote = document.getElementById('statusNote');
    const crumbs = document.getElementById('crumbs');
    const fileRows = document.getElementById('fileRows');
    const fileSummary = document.getElementById('fileSummary');
    const gammaCtrl = document.getElementById('gammaCtrl');
    const gammaVal = document.getElementById('gammaVal');
    const uploadModeSel = document.getElementById('uploadMode');
    const ditherModeSel = document.getElementById('ditherMode');
    const uploadExtra = document.getElementById('uploadExtra');
    const uploadBtn = document.querySelector("button[onclick='uploadFile()']");
    const uploadBox = document.getElementById('uploadBox');
    const ditherHint = document.getElementById('ditherHint');
    const gammaHint = document.getElementById('gammaHint');
    const modeNote = document.getElementById('modeNote');
    let currentDir = '/pic';

    function setNotice(text) {
      if (!statusNote) return;
      if (!text) {
        statusNote.style.display = 'none';
        statusNote.textContent = '';
        return;
      }
      statusNote.style.display = 'block';
      statusNote.textContent = text;
    }

    const syncGammaLabel = () => {
      if (!gammaCtrl || !gammaVal) return;
      const g = Number.parseFloat(gammaCtrl.value);
      gammaVal.textContent = Number.isFinite(g) ? g.toFixed(2) : '1.00';
    };
    const ditherAdvice = {
      fs_serpentine: '通用默认。人像、日常照片优先用这个，边缘和肤色过渡更自然。',
      atkinson: '对比更干净，适合图标、漫画、线稿和高反差插画。',
      jjn: '扩散更平滑，适合风景照、大面积渐变和天空云层。'
    };
    function normalizeDir(path) {
      let out = path || '/';
      if (!out.startsWith('/')) out = '/' + out;
      out = out.replace(/\/+/g, '/');
      if (out.length > 1 && out.endsWith('/')) out = out.slice(0, -1);
      return out || '/';
    }
    function parentDir(path) {
      const dir = normalizeDir(path);
      if (dir === '/') return '/';
      const idx = dir.lastIndexOf('/');
      return idx <= 0 ? '/' : dir.slice(0, idx);
    }
    function joinPath(dir, name){
      const base = normalizeDir(dir);
      if (!name) return base;
      if (name.startsWith('/')) return normalizeDir(name);
      return normalizeDir((base === '/' ? '' : base) + '/' + name);
    }
    function formatSize(bytes) {
      const n = Number(bytes || 0);
      if (n < 1024) return `${n} B`;
      if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
      return `${(n / (1024 * 1024)).toFixed(1)} MB`;
    }
    function renderBreadcrumb(path) {
      const dir = normalizeDir(path);
      crumbs.innerHTML = '';
      const parts = dir === '/' ? [] : dir.slice(1).split('/');
      const rootBtn = document.createElement('button');
      rootBtn.className = 'crumb' + (dir === '/' ? ' current' : '');
      rootBtn.textContent = '/';
      rootBtn.onclick = () => openDir('/');
      crumbs.appendChild(rootBtn);
      let acc = '';
      parts.forEach((part, idx) => {
        const sep = document.createElement('span');
        sep.className = 'small';
        sep.textContent = '/';
        crumbs.appendChild(sep);
        acc += '/' + part;
        const btn = document.createElement('button');
        btn.className = 'crumb' + (idx === parts.length - 1 ? ' current' : '');
        btn.textContent = part;
        const target = acc;
        btn.onclick = () => openDir(target);
        crumbs.appendChild(btn);
      });
    }
    function syncGammaHint() {
      if (!gammaHint || !gammaCtrl) return;
      const g = Number.parseFloat(gammaCtrl.value);
      if (!Number.isFinite(g)) {
        gammaHint.textContent = '当前为默认值。';
      } else if (g < 0.95) {
        gammaHint.textContent = '当前偏亮：暗部会被抬起，适合原图偏暗，但可能降低整体对比。';
      } else if (g <= 1.08) {
        gammaHint.textContent = '当前均衡：适合作为通用起点，先看效果再微调。';
      } else if (g <= 1.20) {
        gammaHint.textContent = '当前偏重：中间调和颜色更扎实，适合风景和高亮场景。';
      } else {
        gammaHint.textContent = '当前较重：画面对比更强，但暗部细节更容易丢失。';
      }
    }
    function syncUploadOptions() {
      const mode = uploadModeSel ? uploadModeSel.value : 'normal';
      const isImageMode = mode === 'fit' || mode === 'crop';
      if (uploadExtra) uploadExtra.classList.toggle('show', isImageMode);
      if (ditherHint && ditherModeSel) {
        ditherHint.textContent = ditherAdvice[ditherModeSel.value] || '';
      }
      if (modeNote) {
        if (mode === 'normal') {
          modeNote.textContent = '普通文件上传会直接保存到当前浏览目录。';
        } else if (mode === 'fit') {
          modeNote.textContent = '图片缩放上传会按比例适配到 800x480，并生成 .epd4 文件后保存到 /pic。';
        } else {
          modeNote.textContent = '图片裁剪上传会优先铺满 800x480，可能裁掉边缘，再生成 .epd4 文件后保存到 /pic。';
        }
      }
      syncGammaHint();
    }
    if (uploadModeSel) {
      uploadModeSel.value = 'normal';
      uploadModeSel.addEventListener('change', syncUploadOptions);
    }
    if (ditherModeSel) {
      ditherModeSel.value = 'fs_serpentine';
      ditherModeSel.addEventListener('change', syncUploadOptions);
    }
    if (gammaCtrl) {
      gammaCtrl.addEventListener('input', syncGammaLabel);
      gammaCtrl.addEventListener('input', syncGammaHint);
    }
    syncGammaLabel();
    syncUploadOptions();

    function fmtMs(ms){
      const s = Math.floor((ms||0)/1000);
      const h = Math.floor(s/3600);
      const m = Math.floor((s%3600)/60);
      const ss = s%60;
      return `${h}h ${m}m ${ss}s`;
    }
    async function loadStatus() {
      const r = await fetch('/api/status');
      const txt = await r.text();
      try {
        const j = JSON.parse(txt);
        document.getElementById('st_mode').textContent = j.state || '--';
        document.getElementById('st_ip').textContent = j.ip || '--';
        document.getElementById('st_apip').textContent = j.ap_ip || '--';
        document.getElementById('st_sd').textContent = j.sd_ready ? '已挂载' : '未挂载';
        document.getElementById('st_uptime').textContent = fmtMs(j.uptime_ms || 0);
        const tmo = j.idle_remaining_ms ?? j.connect_remaining_ms ?? j.session_remaining_ms ?? 0;
        document.getElementById('st_timeout').textContent = fmtMs(tmo);
        document.getElementById('st_temp').textContent = (typeof j.temperature_c === 'number' && j.temperature_c >= -100) ? `${j.temperature_c.toFixed(1)} °C` : '未接入';
        const mv = Number(j.battery_mv || 0);
        const pct = Number(j.battery_pct);
        const pin = j.battery_pin ?? '?';
        if (mv > 0 && Number.isFinite(pct) && pct > 0.1) {
          document.getElementById('st_battery').textContent = `${pct.toFixed(1)}%（${mv} mV，IO${pin}）`;
        } else if (mv > 0) {
          document.getElementById('st_battery').textContent = `${mv} mV（IO${pin}，仅显示电压）`;
        } else {
          document.getElementById('st_battery').textContent = '未接入';
        }
        setNotice('');
      } catch {
        setNotice(txt || '状态读取失败');
      }
    }

    async function loadCfg() {
      const r = await fetch('/api/settings');
      const j = await r.json();
      ssid.value = j.sta_ssid || '';
      pass.value = j.sta_pass || '';
      tz.value = j.timezone || '';
      sec.value = j.photo_interval_sec || 300;
      wurl.value = j.weather_url || '';
    }

    async function saveCfg() {
      const body = `sta_ssid=${encodeURIComponent(ssid.value)}&sta_pass=${encodeURIComponent(pass.value)}&timezone=${encodeURIComponent(tz.value)}&photo_interval_sec=${encodeURIComponent(sec.value)}&weather_url=${encodeURIComponent(wurl.value)}`;
      const r = await fetch('/api/settings', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body });
      document.getElementById('cfgBox').textContent = await r.text();
    }

    async function testWeather() {
      const r = await fetch('/api/weather_test');
      document.getElementById('cfgBox').textContent = await r.text();
    }

    async function stopPortal() {
      const r = await fetch('/api/stop', { method:'POST' });
      const txt = await r.text();
      let msg = '停止请求已发送。';
      try {
        const j = JSON.parse(txt);
        if (j && j.ok) {
          msg = j.stopping ? '停止请求已发送，WiFi 门户即将关闭。' : '请求已完成。';
        }
      } catch {}
      setNotice(msg);
    }

    async function openDir(path) {
      currentDir = normalizeDir(path);
      await listFiles(currentDir);
    }
    function goRoot() {
      openDir('/');
    }
    function goUpDir() {
      openDir(parentDir(currentDir));
    }
    async function listFiles(path) {
      const dir = normalizeDir(path || currentDir || '/pic');
      currentDir = dir;
      renderBreadcrumb(dir);
      const r = await fetch('/api/files?path=' + encodeURIComponent(dir));
      const txt = await r.text();
      let j = null;
      try { j = JSON.parse(txt); } catch {}
      fileRows.innerHTML = '';
      if (!j || !j.ok || !Array.isArray(j.items)) {
        fileSummary.textContent = txt || '目录读取失败';
        return;
      }
      currentDir = normalizeDir(j.path || dir);
      renderBreadcrumb(currentDir);
      const items = [...j.items].sort((a, b) => {
        if (!!a.dir !== !!b.dir) return a.dir ? -1 : 1;
        return String(a.name || '').localeCompare(String(b.name || ''), 'zh-Hans-CN');
      });
      let count = 0;
      items.forEach(it => {
        count++;
        const p = joinPath(currentDir, it.name || '');
        const tr = document.createElement('tr');
        tr.innerHTML = `<td></td><td>${it.dir ? '--' : formatSize(it.size || 0)}</td><td><span class="badge${it.dir ? ' dir' : ''}">${it.dir ? '目录' : '文件'}</span></td><td class='actions'></td>`;
        const nameCell = tr.children[0];
        const nameBtn = document.createElement('button');
        nameBtn.className = 'name-btn' + (it.dir ? ' dir' : '');
        nameBtn.textContent = it.name || '';
        if (it.dir) {
          nameBtn.onclick = () => openDir(p);
          nameBtn.ondblclick = () => openDir(p);
        } else {
          nameBtn.onclick = () => window.open('/api/file?path=' + encodeURIComponent(p), '_blank');
        }
        nameCell.appendChild(nameBtn);
        const ops = tr.children[3];
        if (it.dir) {
          const openBtn = document.createElement('button');
          openBtn.className = 'btn';
          openBtn.textContent = '进入';
          openBtn.onclick = () => openDir(p);
          ops.appendChild(openBtn);
        } else {
          const a = document.createElement('a');
          a.href = '/api/file?path=' + encodeURIComponent(p);
          a.textContent = '下载';
          a.style.marginRight = '8px';
          ops.appendChild(a);
          const del = document.createElement('button');
          del.className = 'btn';
          del.textContent = '删除';
          del.onclick = async () => {
            const rr = await fetch('/api/file?path=' + encodeURIComponent(p), { method:'DELETE' });
            alert(await rr.text());
            listFiles(currentDir);
          };
          ops.appendChild(del);
        }
        fileRows.appendChild(tr);
      });
      fileSummary.textContent = `当前位置：${currentDir}，共 ${count} 项`;
    }

    function dist2Weighted(a, b) {
      const dr = a[0] - b[0];
      const dg = a[1] - b[1];
      const db = a[2] - b[2];
      return 3 * dr * dr + 6 * dg * dg + db * db;
    }

    function nearestTwoPalette(rgb, palette) {
      let first = 0;
      let second = 0;
      let best = Number.MAX_SAFE_INTEGER;
      let next = Number.MAX_SAFE_INTEGER;
      for (let pi = 0; pi < palette.length; pi++) {
        const d = dist2Weighted(rgb, palette[pi].rgb);
        if (d < best) {
          next = best;
          second = first;
          best = d;
          first = pi;
        } else if (d < next) {
          next = d;
          second = pi;
        }
      }
      return { first, second, best, next };
    }

    function quantizeImageToEpd4(rgba, W, H, palette, ditherMode, gammaValue) {
      const out = new Uint8Array((W * H) >> 1);
      const pix = new Uint8Array(W * H);

      // Error-diffusion working buffer.
      const work = new Float32Array(W * H * 3);
      const gamma = (Number.isFinite(gammaValue) && gammaValue > 0) ? gammaValue : 1.0;
      const invGamma = 1.0 / gamma;
      for (let i = 0, p = 0; i < W * H; i++, p += 4) {
        const a = rgba[p + 3];
        let r = (a < 8) ? 255 : rgba[p + 0];
        let g = (a < 8) ? 255 : rgba[p + 1];
        let b = (a < 8) ? 255 : rgba[p + 2];
        r = 255 * Math.pow(r / 255, invGamma);
        g = 255 * Math.pow(g / 255, invGamma);
        b = 255 * Math.pow(b / 255, invGamma);
        work[i * 3 + 0] = r;
        work[i * 3 + 1] = g;
        work[i * 3 + 2] = b;
      }

      const clamp255 = (v) => (v < 0 ? 0 : (v > 255 ? 255 : v));
      const addErr = (x, y, er, eg, eb, ratio) => {
        if (x < 0 || x >= W || y < 0 || y >= H) return;
        const idx = (y * W + x) * 3;
        work[idx + 0] += er * ratio;
        work[idx + 1] += eg * ratio;
        work[idx + 2] += eb * ratio;
      };

      const mode = (ditherMode || 'fs_serpentine').toLowerCase();
      for (let y = 0; y < H; y++) {
        const reverse = (mode === 'fs_serpentine' || mode === 'jjn') && ((y & 1) === 1);
        const xStart = reverse ? (W - 1) : 0;
        const xEnd = reverse ? -1 : W;
        const step = reverse ? -1 : 1;
        for (let x = xStart; x !== xEnd; x += step) {
          const wi = (y * W + x) * 3;
          const rgb = [
            clamp255(work[wi + 0]),
            clamp255(work[wi + 1]),
            clamp255(work[wi + 2]),
          ];
          const pick = nearestTwoPalette(rgb, palette);
          const chosen = palette[pick.first].rgb;
          const er = rgb[0] - chosen[0];
          const eg = rgb[1] - chosen[1];
          const eb = rgb[2] - chosen[2];
          pix[y * W + x] = palette[pick.first].nib;

          if (mode === 'fs_serpentine') {
            if (!reverse) {
              addErr(x + 1, y, er, eg, eb, 7 / 16);
              addErr(x - 1, y + 1, er, eg, eb, 3 / 16);
              addErr(x, y + 1, er, eg, eb, 5 / 16);
              addErr(x + 1, y + 1, er, eg, eb, 1 / 16);
            } else {
              addErr(x - 1, y, er, eg, eb, 7 / 16);
              addErr(x + 1, y + 1, er, eg, eb, 3 / 16);
              addErr(x, y + 1, er, eg, eb, 5 / 16);
              addErr(x - 1, y + 1, er, eg, eb, 1 / 16);
            }
          } else if (mode === 'atkinson') {
            const r = 1 / 8;
            addErr(x + 1, y, er, eg, eb, r);
            addErr(x + 2, y, er, eg, eb, r);
            addErr(x - 1, y + 1, er, eg, eb, r);
            addErr(x, y + 1, er, eg, eb, r);
            addErr(x + 1, y + 1, er, eg, eb, r);
            addErr(x, y + 2, er, eg, eb, r);
          } else if (mode === 'jjn') {
            if (!reverse) {
              addErr(x + 1, y, er, eg, eb, 7 / 48);
              addErr(x + 2, y, er, eg, eb, 5 / 48);
              addErr(x - 2, y + 1, er, eg, eb, 3 / 48);
              addErr(x - 1, y + 1, er, eg, eb, 5 / 48);
              addErr(x, y + 1, er, eg, eb, 7 / 48);
              addErr(x + 1, y + 1, er, eg, eb, 5 / 48);
              addErr(x + 2, y + 1, er, eg, eb, 3 / 48);
              addErr(x - 2, y + 2, er, eg, eb, 1 / 48);
              addErr(x - 1, y + 2, er, eg, eb, 3 / 48);
              addErr(x, y + 2, er, eg, eb, 5 / 48);
              addErr(x + 1, y + 2, er, eg, eb, 3 / 48);
              addErr(x + 2, y + 2, er, eg, eb, 1 / 48);
            } else {
              addErr(x - 1, y, er, eg, eb, 7 / 48);
              addErr(x - 2, y, er, eg, eb, 5 / 48);
              addErr(x + 2, y + 1, er, eg, eb, 3 / 48);
              addErr(x + 1, y + 1, er, eg, eb, 5 / 48);
              addErr(x, y + 1, er, eg, eb, 7 / 48);
              addErr(x - 1, y + 1, er, eg, eb, 5 / 48);
              addErr(x - 2, y + 1, er, eg, eb, 3 / 48);
              addErr(x + 2, y + 2, er, eg, eb, 1 / 48);
              addErr(x + 1, y + 2, er, eg, eb, 3 / 48);
              addErr(x, y + 2, er, eg, eb, 5 / 48);
              addErr(x - 1, y + 2, er, eg, eb, 3 / 48);
              addErr(x - 2, y + 2, er, eg, eb, 1 / 48);
            }
          }
        }
      }
      let outIdx = 0;
      for (let i = 0; i < W * H; i += 2) {
        out[outIdx++] = ((pix[i] & 0x0F) << 4) | (pix[i + 1] & 0x0F);
      }
      return out;
    }

    async function preprocessImageToEpd4Blob(file, cropMode, ditherMode = 'fs_serpentine', gammaValue = 1.0) {
      const img = new Image();
      const dataUrl = await new Promise((resolve, reject) => {
        const fr = new FileReader();
        fr.onload = () => resolve(fr.result);
        fr.onerror = () => reject(new Error('read_failed'));
        fr.readAsDataURL(file);
      });
      await new Promise((resolve, reject) => {
        img.onload = () => resolve();
        img.onerror = () => reject(new Error('decode_failed'));
        img.src = dataUrl;
      });

      const W = 800;
      const H = 480;
      const canvas = document.createElement('canvas');
      canvas.width = W;
      canvas.height = H;
      const ctx = canvas.getContext('2d', { willReadFrequently: true });
      const srcLandscape = img.width >= img.height;
      const frameLandscape = W >= H;
      const rotate90 = (srcLandscape !== frameLandscape);
      const srcW = rotate90 ? img.height : img.width;
      const srcH = rotate90 ? img.width : img.height;
      const sx = W / srcW;
      const sy = H / srcH;
      const scale = cropMode ? Math.max(sx, sy) : Math.min(sx, sy);
      const dw = srcW * scale;
      const dh = srcH * scale;
      ctx.fillStyle = '#ffffff';
      ctx.fillRect(0, 0, W, H);
      if (!rotate90) {
        const dx = (W - dw) * 0.5;
        const dy = (H - dh) * 0.5;
        ctx.drawImage(img, dx, dy, dw, dh);
      } else {
        // Rotate portrait input to landscape first, then apply fit/crop scaling.
        // Use -90deg to match expected orientation on this frame.
        ctx.save();
        ctx.translate(W * 0.5, H * 0.5);
        ctx.rotate(-Math.PI / 2);
        ctx.drawImage(img, -dh * 0.5, -dw * 0.5, dh, dw);
        ctx.restore();
      }
      const rgba = ctx.getImageData(0, 0, W, H).data;

      const paletteNative = [
        { rgb:[0,0,0], nib:0x00 },
        { rgb:[255,255,255], nib:0x01 },
        { rgb:[255,255,0], nib:0x02 }, // yellow
        { rgb:[255,0,0], nib:0x03 },   // red
        { rgb:[0,0,255], nib:0x05 },   // blue
        { rgb:[0,255,0], nib:0x06 },   // green
      ];
      const palette = paletteNative;

      const out = quantizeImageToEpd4(rgba, W, H, palette, ditherMode, gammaValue);
      return new Blob([out], { type: 'application/octet-stream' });
    }

    async function uploadFile() {
      const dir = currentDir || '/';
      const mode = document.getElementById('uploadMode').value || 'normal';
      const ditherMode = document.getElementById('ditherMode')
        ? (document.getElementById('ditherMode').value || 'fs_serpentine')
        : 'fs_serpentine';
      const gammaValue = document.getElementById('gammaCtrl')
        ? Number.parseFloat(document.getElementById('gammaCtrl').value || '1.0')
        : 1.0;
      const gammaText = Number.isFinite(gammaValue) ? gammaValue.toFixed(2) : '1.00';
      const f = document.getElementById('uploadFile').files[0];
      if (uploadBox && f) {
        uploadBox.textContent = `开始上传：模式=${mode}，算法=${ditherMode}，Gamma=${gammaText}，文件=${f.name || 'unknown'}`;
      }
      console.log('[UPLOAD]', { mode, ditherMode, gamma: gammaText, file: f ? (f.name || '') : '' });
      if (!f) {
        document.getElementById('uploadBox').textContent = '请选择要上传的文件';
        return;
      }
      if (mode === 'normal') {
        if (uploadBtn) uploadBtn.disabled = true;
        const fd = new FormData();
        fd.append('file', f);
        const q = '/api/upload?dir=' + encodeURIComponent(dir) + '&mode=normal'
          + '&algo=' + encodeURIComponent(ditherMode)
          + '&gamma=' + encodeURIComponent(gammaText);
        const r = await fetch(q, { method:'POST', body:fd });
        document.getElementById('uploadBox').textContent = await r.text();
        listFiles(currentDir);
        if (uploadBtn) uploadBtn.disabled = false;
        return;
      }
      const lower = (f.name || '').toLowerCase();
      if (!(lower.endsWith('.jpg') || lower.endsWith('.jpeg') || lower.endsWith('.png') || lower.endsWith('.bmp'))) {
        document.getElementById('uploadBox').textContent = '缩放/裁剪上传仅支持 bmp/jpg/png';
        return;
      }
      document.getElementById('uploadBox').textContent = '正在预处理并转换为 EPD4...';
      if (uploadBtn) uploadBtn.disabled = true;
      try {
        const epdBlob = await preprocessImageToEpd4Blob(f, mode === 'crop', ditherMode, gammaValue);
        const outName = (f.name.replace(/\.[^.]+$/, '') || 'image') + '.epd4';
        const fd = new FormData();
        fd.append('file', epdBlob, outName);
        const q = '/api/upload?dir=' + encodeURIComponent('/pic') + '&mode=normal'
          + '&algo=' + encodeURIComponent(ditherMode)
          + '&gamma=' + encodeURIComponent(gammaText);
        const r = await fetch(q, { method:'POST', body:fd });
        document.getElementById('uploadBox').textContent = await r.text();
        currentDir = '/pic';
        listFiles('/pic');
        if (uploadBtn) uploadBtn.disabled = false;
      } catch (e) {
        if (uploadBtn) uploadBtn.disabled = false;
        document.getElementById('uploadBox').textContent = '预处理失败: ' + (e && e.message ? e.message : String(e));
      }
    }

    loadStatus();
    loadCfg();
    listFiles();
  </script>
</body>
</html>
)HTML";
  server_->send(200, "text/html", html);
}

void WifiManager::handleStatus() {
  markActivity(millis());
  Serial.printf("[HTTP] GET /api/status from %s\n",
                server_->client().remoteIP().toString().c_str());
  const char *state_str = "idle";
  if (state_ == State::ApRunning) state_str = "ap_running";
  if (state_ == State::StaConnecting) state_str = "sta_connecting";
  if (state_ == State::StaRunning) state_str = "sta_running";
  String json = "{";
  json += "\"state\":\"";
  json += state_str;
  json += "\",\"ip\":\"";
  json += jsonEscape(WiFi.localIP().toString());
  json += "\",\"ap_ip\":\"";
  json += jsonEscape(WiFi.softAPIP().toString());
  json += "\",\"sd_ready\":";
  json += sd_ready_ ? "true" : "false";
  json += ",\"uptime_ms\":";
  json += String(millis());
  json += ",\"temperature_c\":";
  if (isnan(temperature_c_)) {
    json += "-1000";
  } else {
    json += String(temperature_c_, 1);
  }
  json += ",\"battery_mv\":";
  json += String(battery_mv_);
  json += ",\"battery_pin\":";
  json += String(battery_adc_pin_);
  json += ",\"battery_pct\":";
  const float battery_pct = estimateBatteryPercent(battery_mv_);
  if (battery_pct < 0.0f) {
    json += "-1";
  } else {
    json += String(battery_pct, 1);
  }
  if (state_ == State::ApRunning) {
    const uint32_t remain =
        (millis() - last_activity_ms_ >= kApIdleTimeoutMs)
            ? 0
            : (kApIdleTimeoutMs - (millis() - last_activity_ms_));
    json += ",\"idle_remaining_ms\":";
    json += String(remain);
  } else if (state_ == State::StaConnecting) {
    const uint32_t remain =
        (millis() - sta_connect_start_ms_ >= kStaConnectTimeoutMs)
            ? 0
            : (kStaConnectTimeoutMs - (millis() - sta_connect_start_ms_));
    json += ",\"connect_remaining_ms\":";
    json += String(remain);
  } else if (state_ == State::StaRunning) {
    const uint32_t remain =
        (millis() - last_activity_ms_ >= kStaSessionTimeoutMs)
            ? 0
            : (kStaSessionTimeoutMs - (millis() - last_activity_ms_));
    json += ",\"idle_remaining_ms\":";
    json += String(remain);
  }
  json += "}";
  server_->send(200, "application/json", json);
}

void WifiManager::handleSettingsGet() {
  markActivity(millis());
  Serial.printf("[HTTP] GET /api/settings from %s\n",
                server_->client().remoteIP().toString().c_str());
  String json = "{";
  json += "\"sta_ssid\":\"" + jsonEscape(settings_.sta_ssid) + "\",";
  json += "\"sta_pass\":\"" + jsonEscape(settings_.sta_pass) + "\",";
  json += "\"timezone\":\"" + jsonEscape(settings_.timezone) + "\",";
  json += "\"photo_interval_sec\":" + String(settings_.photo_interval_sec) + ",";
  json += "\"weather_url\":\"" + jsonEscape(settings_.weather_url) + "\"";
  json += "}";
  server_->send(200, "application/json", json);
}

void WifiManager::handleSettingsPost() {
  markActivity(millis());
  Serial.printf("[HTTP] POST /api/settings from %s\n",
                server_->client().remoteIP().toString().c_str());
  if (server_->hasArg("sta_ssid")) settings_.sta_ssid = server_->arg("sta_ssid");
  if (server_->hasArg("sta_pass")) settings_.sta_pass = server_->arg("sta_pass");
  if (server_->hasArg("timezone")) settings_.timezone = server_->arg("timezone");
  if (server_->hasArg("photo_interval_sec")) {
    settings_.photo_interval_sec = static_cast<uint32_t>(server_->arg("photo_interval_sec").toInt());
    if (settings_.photo_interval_sec < 30) settings_.photo_interval_sec = 30;
    if (settings_.photo_interval_sec > 86400) settings_.photo_interval_sec = 86400;
  }
  if (server_->hasArg("weather_url")) settings_.weather_url = server_->arg("weather_url");

  settings_.sta_ssid.trim();
  settings_.timezone.trim();
  settings_.weather_url.trim();
  if (settings_.sta_ssid.length() == 0) {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"empty_sta_ssid\"}");
    return;
  }
  if (settings_.timezone.length() == 0) {
    settings_.timezone = kDefaultTimezone;
  }
  if (settings_.weather_url.length() == 0) {
    settings_.weather_url = kDefaultWeatherUrl;
  }
  saveSettings();
  server_->send(200, "application/json", "{\"ok\":true}");
}

void WifiManager::handleFilesList() {
  markActivity(millis());
  const String path = server_->hasArg("path") ? server_->arg("path") : "/pic";
  Serial.printf("[HTTP] GET /api/files path=%s\n", path.c_str());
  if (!isSafePath(path)) {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_path\"}");
    return;
  }
  server_->send(200, "application/json", listDirectoryJson(path.c_str()));
}

void WifiManager::handleDirCreate() {
  markActivity(millis());
  if (!sd_ready_) {
    server_->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_ready\"}");
    return;
  }
  if (!server_->hasArg("path")) {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"missing_path\"}");
    return;
  }
  String path = server_->arg("path");
  path.trim();
  Serial.printf("[HTTP] POST /api/dir path=%s\n", path.c_str());
  if (!isSafePath(path) || path == "/") {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_path\"}");
    return;
  }
  if (SD.exists(path)) {
    server_->send(409, "application/json", "{\"ok\":false,\"error\":\"already_exists\"}");
    return;
  }
  const bool ok = SD.mkdir(path);
  server_->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

void WifiManager::handleFileDownload() {
  markActivity(millis());
  if (!sd_ready_) {
    server_->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_ready\"}");
    return;
  }
  if (!server_->hasArg("path")) {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"missing_path\"}");
    return;
  }
  const String path = server_->arg("path");
  Serial.printf("[HTTP] GET /api/file path=%s\n", path.c_str());
  if (!isSafePath(path)) {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_path\"}");
    return;
  }
  File file = SD.open(path, FILE_READ);
  if (!file) {
    server_->send(404, "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
    return;
  }
  server_->streamFile(file, contentTypeForPath(path));
  file.close();
}

void WifiManager::handleFileDelete() {
  markActivity(millis());
  if (!sd_ready_) {
    server_->send(503, "application/json", "{\"ok\":false,\"error\":\"sd_not_ready\"}");
    return;
  }
  if (!server_->hasArg("path")) {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"missing_path\"}");
    return;
  }
  const String path = server_->arg("path");
  Serial.printf("[HTTP] DELETE /api/file path=%s\n", path.c_str());
  if (!isSafePath(path)) {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_path\"}");
    return;
  }
  const bool ok = SD.remove(path);
  server_->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

void WifiManager::handleWeatherTest() {
  markActivity(millis());
  Serial.printf("[HTTP] GET /api/weather_test url=%s\n", settings_.weather_url.c_str());

  if (state_ != State::StaRunning || WiFi.status() != WL_CONNECTED) {
    server_->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"sta_required\",\"msg\":\"weather test requires STA connected\"}");
    return;
  }
  if (!(settings_.weather_url.startsWith("http://") || settings_.weather_url.startsWith("https://"))) {
    server_->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_weather_url\"}");
    return;
  }
  String json = "{";
  json += "\"ok\":true,";
  json += "\"msg\":\"sta_connected_url_format_ok\",";
  json += "\"url\":\"";
  json += jsonEscape(settings_.weather_url);
  json += "\",\"ip\":\"";
  json += jsonEscape(WiFi.localIP().toString());
  json += "\"}";
  server_->send(200, "application/json", json);
}

void WifiManager::handleStopPortal() {
  markActivity(millis());
  Serial.printf("[HTTP] POST /api/stop from %s\n",
                server_->client().remoteIP().toString().c_str());
  server_->send(200, "application/json", "{\"ok\":true,\"stopping\":true}");
  stop("manual_http_stop");
  auto_exit_requested_ = true;
}

void WifiManager::handleFileUpload() {
  markActivity(millis());
  static File upload_file;
  static String upload_algo;
  static String upload_gamma;
  HTTPUpload &upload = server_->upload();

  if (upload.status == UPLOAD_FILE_START) {
    upload_ok_ = true;
    upload_error_ = "";
    upload_mode_ = server_->hasArg("mode") ? server_->arg("mode") : "normal";
    upload_mode_.toLowerCase();
    upload_algo = server_->hasArg("algo") ? server_->arg("algo") : "none";
    upload_gamma = server_->hasArg("gamma") ? server_->arg("gamma") : "1.00";
    upload_tmp_path_ = "";
    Serial.printf("[UPLOAD] begin mode=%s algo=%s gamma=%s remote=%s\n",
                  upload_mode_.c_str(),
                  upload_algo.c_str(),
                  upload_gamma.c_str(),
                  server_->client().remoteIP().toString().c_str());
  }

  if (!sd_ready_) {
    upload_ok_ = false;
    upload_error_ = "sd_not_ready";
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("[UPLOAD] reject err=%s\n", upload_error_.c_str());
    }
    return;
  }

  String filename = upload.filename;
  filename.replace("\\", "");
  filename.replace("/", "");
  if (filename.length() == 0) {
    upload_ok_ = false;
    upload_error_ = "bad_filename";
    return;
  }

  const bool preprocess_mode = (upload_mode_ == "fit" || upload_mode_ == "crop");
  const String dir = server_->hasArg("dir") ? server_->arg("dir") : "/";
  if (!isSafePath(dir)) {
    upload_ok_ = false;
    upload_error_ = "bad_dir";
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("[SD] upload rejected bad dir=%s\n", dir.c_str());
    }
    return;
  }
  String filepath = dir;
  if (!filepath.endsWith("/")) {
    filepath += "/";
  }
  filepath += filename;
  upload_tmp_path_ = filepath;

  if (upload.status == UPLOAD_FILE_START) {
    if (preprocess_mode) {
      upload_ok_ = false;
      upload_error_ = "server_preprocess_disabled_use_browser";
      Serial.printf("[UPLOAD] reject mode=%s err=%s\n", upload_mode_.c_str(),
                    upload_error_.c_str());
      return;
    }
    const int slash = filepath.lastIndexOf('/');
    const String parent = (slash > 0) ? filepath.substring(0, slash) : "/";
    if (parent.length() > 0 && !SD.exists(parent)) {
      SD.mkdir(parent);
    }
    if (SD.exists(filepath)) {
      SD.remove(filepath);
    }
    upload_file = SD.open(filepath, FILE_WRITE);
    if (!upload_file) {
      upload_ok_ = false;
      upload_error_ = "open_failed";
      Serial.printf("[SD] upload open failed %s\n", filepath.c_str());
      return;
    }
    Serial.printf("[SD] upload start %s\n", filepath.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (upload_file) {
      upload_file.write(upload.buf, upload.currentSize);
    } else {
      upload_ok_ = false;
      upload_error_ = "write_target_missing";
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (upload_file) {
      upload_file.close();
    }
    Serial.printf("[SD] upload done %s size=%u mode=%s algo=%s gamma=%s\n",
                  filepath.c_str(), upload.totalSize,
                  upload_mode_.c_str(),
                  upload_algo.c_str(),
                  upload_gamma.c_str());

    if (!upload_ok_) {
      SD.remove(filepath);
      Serial.printf("[UPLOAD] end with prior error=%s cleaned=%s\n", upload_error_.c_str(),
                    filepath.c_str());
      return;
    }

    if (upload_ok_) {
      Serial.printf("[UPLOAD] done ok mode=%s algo=%s gamma=%s final=%s\n",
                    upload_mode_.c_str(),
                    upload_algo.c_str(),
                    upload_gamma.c_str(),
                    filepath.c_str());
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    upload_ok_ = false;
    upload_error_ = "aborted";
    if (upload_file) {
      upload_file.close();
    }
    if (upload_tmp_path_.length() > 0) {
      SD.remove(upload_tmp_path_);
      Serial.printf("[SD] upload aborted %s\n", upload_tmp_path_.c_str());
    }
    Serial.printf("[UPLOAD] aborted err=%s\n", upload_error_.c_str());
  }
}

void WifiManager::handleNotFound() {
  markActivity(millis());
  server_->send(404, "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
}

}  // namespace appfw
