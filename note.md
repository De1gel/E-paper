
测试中的问题： 

英文地名

[WEATHER] code missing
[TIME] weather sync failed tz=Europe/Zurich err=empty_timezone
[HTTP] GET /api/geocode city=洛桑
[HTTP] GET /api/weather_test url=http://api.open-meteo.com/v1/forecast?latitude=46.516&longitude=6.63282&current=temperature_2m,relative_humidity_2m,weather_code&timezone=auto
[HTTP] GET /api/weather_test url=http://api.open-meteo.com/v1/forecast?latitude=46.516&longitude=6.63282&current=temperature_2m,relative_humidity_2m,weather_code&timezone=auto
[WEATHER] code missing
[TIME] weather sync failed tz=Europe/Zurich err=empty_timezone
[HTTP] GET /api/weather_test url=http://api.open-meteo.com/v1/forecast?latitude=46.516&longitude=6.63282&current=temperature_2m,relative_humidity_2m,weather_code&timezone=auto
[WEATHER] code missing
[TIME] weather sync failed tz=Europe/Zurich err=empty_timezone
[HTTP] GET /api/geocode city=北京
[HTTP] GET /api/weather_test url=http://api.open-meteo.com/v1/forecast?latitude=46.516&longitude=6.63282&current=temperature_2m,relative_humidity_2m,weather_code&timezone=auto
[WEATHER] code missing
[TIME] weather sync failed tz=Europe/Zurich err=empty_timezone
[HTTP] GET /api/geocode city=北京
[HTTP] GET /api/weather_test url=http://api.open-meteo.com/v1/forecast?latitude=46.516&longitude=6.63282&current=temperature_2m,relative_humidity_2m,weather_code&timezone=auto
[WEATHER] code missing
[TIME] weather sync failed tz=Europe/Zurich err=empty_timezone
[HTTP] POST /api/settings from 192.168.4.2
[CFG] settings saved




[OP][START] id=11 source=user action=MidLong state=Calendar mode=ConfigAP t=953700ms
[INPUT] event=MidLong
[CONFIG] long press exit config + stop wifi
[CONFIG] ConfigAP -> Normal
[OP][END] id=11 result=ok elapsed=11ms


[WIFI] stop reason=manual_key_exit_config
[SD] unmounted reason=wifi_manager
[CONFIG] WiFi exited with saved settings -> full refresh queued reason=manual_key_exit_config
[LED] state=normal_on reason=awake
[LED] state=breath reason=calendar_render
[TIME] pre-refresh time sync required
[WIFI] enterprise cert time check=enabled
[WIFI] STA enterprise begin user=23120829
[WIFI] STA connecting reason=calendar_pre_refresh ssid=phone.wlan.bjtu auth=enterprise user_len=8 pass_len=8 timeout=8s initial_status=WL_DISCONNECTED/6
[CAL] pre-refresh sync: starting STA before full refresh
[WIFI] STA status -> WL_IDLE_STATUS (0)
[WIFI] STA status -> WL_CONNECTED (3)
[WIFI] web server started
[SD] mount ok reason=wifi_manager
[WIFI] STA connected ip=10.61.55.37 mdns=http://epaper.local/
[TIME] sync failed weather_err=weather_request_failed tz= err=empty_timezone
[CALSYNC] trigger pending=true due=false interval_s=900 now_ms=964297 url=/team-sync-meeting.ics
[CALSYNC] expanded imported=52
[CFG] settings saved
[CALSYNC] ok vevents=5 imported=52 month=52 stored_ics=0 kept_manual=0 total=0 elapsed=113ms window=1777593600..1780272000
[CAL] pre-refresh sync settled -> proceed render wifi_seen=true
[CAL] render begin partial=false
[POWER] peripheral rail=ON
