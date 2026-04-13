# WiFi Config Portal（按当前代码）

更新时间：2026-04-10

## 1. 入口

- 进入方式：中键长按约 `600ms` 进入 `ConfigWait`
- `ConfigWait` 中：
  - 上键短按：进入 AP Portal
  - 下键短按：进入 STA Portal
  - 中键短按：执行白屏动作并返回 `Normal`
- AP 模式：连接 `PhotoFrame_Config / 12345678`，打开 `http://192.168.4.1/`
- STA 模式：设备连接已保存路由器后，打开 `http://<device_ip>/`

Portal 页面优先从 SPIFFS 提供：
- `data/portal.html`
- `data/app.js`

如果未执行 `uploadfs`，固件会回退到内嵌最小提示页。

## 2. 当前默认设置（来自代码）

以下是当前代码内置默认值，部署前建议改掉：

- AP SSID：`PhotoFrame_Config`
- AP 密码：`12345678`
- STA SSID：`DESKTOP-09PTMRM 4607`
- STA 密码：`67O9b1-2`
- `ui_language`：`zh`
- `timezone`：`Asia/Shanghai`
- `photo_interval_sec`：`3600`
- `calendar_enabled`：`false`
- `calendar_layout`：`landscape_split`
- `calendar_refresh_sec`：`900`
- `calendar_url`：空字符串
- `weather_city`：`Shanghai`
- `weather_lat`：`31.2304`
- `weather_lon`：`121.4737`
- `weather_url`：Open-Meteo 上海默认 URL

## 3. API 清单

### 状态与设置

- `GET /api/status`
- `GET /api/settings`
- `POST /api/settings`

`/api/settings` 当前主要字段：
- `sta_ssid`
- `sta_pass`
- `ui_language`
- `photo_interval_sec`
- `calendar_enabled`
- `calendar_layout`
- `calendar_refresh_sec`
- `calendar_url`
- `weather_city`
- `weather_lat`
- `weather_lon`
- `weather_url`

### 日程

- `GET /api/calendar/events`
- `POST /api/calendar/events`
- `DELETE /api/calendar/events?id=<id>`

当前支持：
- `once`
- `daily`
- `weekly`

### 文件与目录

- `GET /api/files?path=/pic`
- `POST /api/dir`
- `GET /api/file?path=/pic/xxx.epd4`
- `DELETE /api/file?path=/pic/xxx.epd4`
- `POST /api/upload?dir=/pic`

### 联网辅助

- `GET /api/geocode?city=<name>`
- `GET /api/weather_test`
- `POST /api/stop`
- `POST /api/reboot`

## 4. `/api/status` 当前返回要点

当前状态页会显示：
- `state`
- `ip`
- `ap_ip`
- `sd_ready`
- `sd_total_bytes`
- `sd_used_bytes`
- `uptime_ms`
- `temperature_c`
- `humidity_pct`
- `battery_mv`
- `battery_pin`
- `battery_pct`

不同状态下还会附带：
- AP：`idle_remaining_ms`
- STA Connecting：`connect_remaining_ms`
- STA Running：`idle_remaining_ms`

说明：
- 当前 `STA Running` 的空闲超时常量为 `0`，因此该字段一般表现为 `0`

## 5. 上传行为

### 固件侧

- `mode=normal`：按原样保存上传文件
- 固件当前不做图片缩放 / 裁剪 / 抖动预处理
- 如果直接请求 `mode=fit` 或 `mode=crop`，固件会返回：
  - `server_preprocess_disabled_use_browser`

### 浏览器侧

Portal 前端已经实现图片预处理：
- `fit` / `crop` 在浏览器 canvas 中完成
- 处理后生成 `.epd4`
- 最终仍以普通上传方式写入 SD 卡

## 6. 超时与自动退出

- `ConfigWait`：`60s`
- AP 空闲超时：`300s`
- AP 模式下如果有 STA 连接，活动时间会持续刷新
- STA 连接超时：`30s`
- STA 运行态空闲超时：当前关闭（`kStaSessionTimeoutMs = 0`）

## 7. 说明与限制

- SD 卡使用 SPI 引脚：`CS=5, SCK=18, MISO=19, MOSI=23`
- 路径检查已启用，`../` 类路径会被拒绝
- `calendar_url` 当前仅做配置持久化，不参与实际同步
- `calendar_enabled` 当前已持久化，但没有真正关闭日历页运行分支
- WiFi 会话期间 `IO32` 外设电源会保持开启，以支持 SD、传感器和门户访问
