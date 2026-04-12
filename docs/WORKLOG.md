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
