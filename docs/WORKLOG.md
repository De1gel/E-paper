# Worklog

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
