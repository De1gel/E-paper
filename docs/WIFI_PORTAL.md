# WiFi Config Portal (Current Implementation)

## Entry
- AP mode: connect `PhotoFrame_Config` / `12345678`, open `http://192.168.4.1/`
- STA mode: device connects to configured router, open `http://<device_ip>/`

## Default Settings
- `sta_ssid`: `YOUR_WIFI_SSID`
- `sta_pass`: `YOUR_WIFI_PASSWORD`
- `timezone`: `Asia/Shanghai`
- `photo_interval_sec`: `300`
- `weather_url`: `http://192.168.4.1/api/weather`

## APIs
- `GET /api/status`
- `GET /api/settings`
- `POST /api/settings`
- `GET /api/files?path=/pic`
- `GET /api/file?path=/pic/xxx.jpg`
- `DELETE /api/file?path=/pic/xxx.jpg`
- `GET /api/weather_test` (lightweight check: STA connected + URL format)
- `POST /api/stop` (manual stop portal, return to normal mode)
- `POST /api/upload?dir=/pic` (multipart form-data, field `file`)

## Timeouts / Auto Exit
- ConfigWait (mode layer): 60s (already existing)
- AP session timeout: 60s -> stop WiFi + force mode back to Normal
- STA connect timeout: 30s -> stop WiFi + force mode back to Normal
- STA portal idle timeout: 120s (no HTTP activity) -> stop WiFi + force mode back to Normal

## Notes
- SD card access currently uses SPI pins: `CS=5, SCK=18, MISO=19, MOSI=23`.
- If SD init fails, file APIs return `sd_not_ready`.
- Weather test endpoint is intentionally lightweight to keep firmware size below flash limit.
