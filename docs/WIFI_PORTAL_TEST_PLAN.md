# WiFi Portal Test Plan (No Upload Stage)

## 1) Config Mode Entry / Exit
- Long press middle key (~600ms) -> enter `ConfigWait`, LED fast blink.
- No key for 60s -> auto exit to `Normal` and serial shows timeout log.
- In `ConfigWait`, short press middle key -> white screen render, then auto exit to `Normal`.

## 2) AP Mode
- In `ConfigWait`, press UP key -> AP starts.
- Connect phone/laptop to `PhotoFrame_Config` / `12345678`.
- Open `http://192.168.4.1/` and verify:
  - `Status` returns JSON.
  - `Settings` can read/write and persists after reboot.
  - `Weather Test` returns lightweight JSON check result.
  - `Stop WiFi Portal` button exits WiFi mode and returns to Normal.
  - `Files` lists `/pic`.
  - Upload one test file to `/pic`.
  - Download and delete a test file.
- Wait 60s without access -> AP auto stops, mode returns `Normal`.

## 3) STA Mode
- Set real `sta_ssid` / `sta_pass` in settings page.
- In `ConfigWait`, press DOWN key -> STA connects.
- Check serial for assigned LAN IP and open `http://<lan_ip>/`.
- Keep idle (no HTTP access) for 120s -> STA portal auto stops, mode returns `Normal`.
- Test wrong password: after 30s connect timeout, STA auto stops and mode returns `Normal`.
- Verify `/api/weather_test` only succeeds when STA is connected and URL format is valid.

## 4) Path Safety
- Call `/api/files?path=../` -> should return `bad_path`.
- Call `/api/file?path=../x` -> should return `bad_path`.

## 5) Regression
- Photo carousel still auto-switches according to `photo_interval_sec`.
- Middle short press still toggles Photo/Calendar page.
- White screen action still shows breathing indicator.
