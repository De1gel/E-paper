# Worklog

## 2026-04-27
- Consolidated the current calendar-focused iteration into a commit candidate after repeated on-device tuning across layout, rendering, weather display, and connectivity behavior.
- Calendar page now uses the editorial-style header card, larger fixed today highlight, per-day event summaries, refined schedule cards, and layered vector weather icons with partial-refresh-aware header redraw paths.
- Startup behavior keeps the short automatic STA pre-sync timeout (`8s`) so a missing AP no longer blocks the first local calendar render for ~30 seconds.
- Geocode behavior was explicitly restored to the pre-experiment version: weather location display remains the resolved single city string rather than a composed province/city/district label.
- Build/verification result before commit: `./test/run_logic_tests.sh` passed, `esp32dev` build passed, RAM `20.0%` (`65448 / 327680`), Flash `34.0%` (`2494737 / 7340032`), firmware uploaded to `/dev/ttyUSB1`, boot render completed and then entered `light sleep` normally.

## 2026-04-12
- Built `esp32dev` successfully after removing quote support from the calendar path and collapsing text AA to `Threshold` plus `Burkes` only.
- Calendar schedule area no longer reserves quote/footer space for `格言`; the landscape AA panel now shows only the `Burkes` sample row.
- Build result: RAM `17.4%` (`57152 / 327680`), Flash `33.1%` (`2430517 / 7340032`).
- Built `esp32dev` successfully after disabling the temporary Burkes comparison panel and restoring the normal schedule page on the right side.
- Build result: RAM `17.4%` (`57152 / 327680`), Flash `33.1%` (`2430041 / 7340032`).
- Built `esp32dev` successfully after adding a full-width top status bar for calendar view with date, time, weather city, and temperature/humidity.
- Calendar layout now reserves a dedicated header bar above both panels; weather and sensor data are read from `WifiManager` getters.
- Build result: RAM `17.5%` (`57216 / 327680`), Flash `33.1%` (`2431601 / 7340032`).
- Built `esp32dev` successfully after moving the calendar status bar back into the left calendar panel and removing the duplicate large date line.
- Calendar top info now renders only inside the calendar panel header; the right schedule panel no longer has a screen-wide shared header.
- Build result: RAM `17.5%` (`57216 / 327680`), Flash `33.1%` (`2431449 / 7340032`).
- Built `esp32dev` successfully after increasing the left calendar header and weekday labels to `24px`, and reducing day numbers inside the month grid to `12px`.
- Calendar layout header and weekday row heights were expanded to match the larger top text sizes.
- Build result: RAM `17.5%` (`57216 / 327680`), Flash `33.1%` (`2431421 / 7340032`).
- Built `esp32dev` successfully after splitting the calendar info bar into two rows (`date/time` on the left, `weather/sensors` on the right).
- Removed month-grid boxes and highlight fills so the calendar area now shows only the day numbers, with a slightly reduced month area height.
- Build result: RAM `17.5%` (`57232 / 327680`), Flash `33.1%` (`2431165 / 7340032`).
- Built `esp32dev` successfully after unifying ASCII glyph calibration so ASCII renders with the same sizing rules under both `Auto` and `CjkAuto`.
- ASCII width, height, and letter spacing now use ASCII-specific metrics instead of inheriting CJK `base_height` during mixed-font rendering.
- Build result: RAM `17.5%` (`57232 / 327680`), Flash `33.1%` (`2431421 / 7340032`).
## 2026-04-13
- Built `esp32dev` successfully with the current pending workspace changes across calendar rendering, portal UI, WiFi/session handling, LED behavior, and project documentation.
- Pending tracked changes reviewed before commit: `data/*`, `src/app/App.cpp`, `src/calendar/*`, `src/system/LedManager.*`, `src/system/WifiManager.cpp`, and multiple `docs/*` updates. Untracked local folders such as `.codex/` and `assets/` remain excluded from version control.
- Build result: RAM `17.5%` (`57232 / 327680`), Flash `33.1%` (`2431969 / 7340032`).
- Updated the calendar month page to hide out-of-month days, use a filled red circle for today, expand the header/weekday rows, and switch the month grid to dynamic `4/5/6` rows while keeping the default boot page on calendar for iteration.
- Replaced stretched ASCII with a native `20px` smooth ASCII font for calendar numerals and header values, then routed header/date/time/weather ASCII rendering through that font without dithering.
- Added true header-time partial refresh using the coordinate-window partial refresh module and a dedicated packed window buffer, so minute ticks can refresh only the time area instead of falling back to a full-screen update.
- Fixed two runtime issues: idle LED no longer starts breathing immediately after upload, and NTP sync no longer triggers an unnecessary second calendar refresh when only sub-minute drift changes.
- Build result: RAM `17.5%` (`57248 / 327680`), Flash `33.3%` (`2442265 / 7340032`).
## 2026-04-14
- Compared the workspace against commit `8729e63` (`Refine calendar rendering and header partial refresh`) before committing the next batch. Effective code delta covers `data/app.js`, `data/index.html`, `data/portal.html`, `src/app/App.cpp`, `src/app/App.h`, `src/display/PartialRefresh.cpp`, `src/system/WifiManager.cpp`, and `src/system/WifiManager.h`.
- Confirmed the official `2026-03-30` Good Display `ESP32-GDEP073E01 - Partial Refresh` sample matches the local partial-refresh sample command chain. Public sample code still performs partial-window RAM writes followed by the long `PON -> BTST2 -> DRF -> POF` refresh sequence, so current header partial updates remain about `22.5s` even for a small window.
- Fixed the partial-window geometry path: partial writes now send the required `DTM` mode byte and normalize horizontal window alignment to `4px`, which removed the row-by-row skew seen in multi-line partial updates. Added a temporary diagnostic pattern path during validation, then restored normal header-time rendering while keeping the alignment fix.
- Tightened and stabilized the header time partial window: the box is now derived from a fixed `"88:88"` sample width instead of the current string width, so the logged refresh area no longer jumps between `88/92/96px`. Added elapsed-time logging for header partial refreshes.
- Fixed an extra full refresh after upload by initializing the periodic calendar refresh timer on the first idle pass instead of immediately queueing a periodic redraw after the boot render.
- Added a new calendar time-refresh configuration field `calendar_time_refresh_sec`, defaulting to `600s` (`10 minutes`). Allowed values are `600`, `1200`, `1800`, `3600`, and `0` (`follow full calendar refresh`). Header partial refreshes now trigger only when the selected time bucket changes; setting `0` disables independent time partial refreshes.
- Updated the web portal and fallback config page to expose the new time-refresh selector with localized labels in Chinese, English, and French. API settings serialization/deserialization now includes `calendar_time_refresh_sec`.
- Built `esp32dev` successfully after the refresh scheduler, partial refresh, and portal config changes.
- Build result: RAM `17.5%` (`57256 / 327680`), Flash `33.3%` (`2443433 / 7340032`).
- Uploaded updated `SPIFFS` assets to the device on `COM4` using `platformio run --target uploadfs --environment esp32dev --upload-port COM4` after stopping a stale `platformio device monitor` process that had the serial port locked.
