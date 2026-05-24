
测试中的问题： 
1 按键功能

按下键，正常出现日志，按上键日志中断，此时切换为照片模式等操作也不会再出现日志，疑似日志线程被中断了，后续需要排查日志线程被中断的原因（按直接按上键唤醒就会出现该问题，先长按中键，再按上键进入AP模式不会出现该问题），某一次按完下键切换照片再按上键又正常了，尝试找到原因

中断
[APP] render done, epd sleep led=breath
[LED] state=sleep_off reason=light_sleep_enter
[SLEEP] enter deadline_in=16596999ms led=sleep_off
[SLEEP][CFG] gpio_wakeup_enable up=0(ESP_OK) mid=0(ESP_OK) down=0(ESP_OK) gpio=0(ESP_OK)
[SLEEP][CFG] timer_wakeup=0(ESP_OK) requested_ms=16597000
[SLEEP][LL] before_sleep pins up=1 mid=1 down=1
[SLEEP][LL] returned err=0 cause=7 slept_ms=151825
[SLEEP][LL] after_wake pins up=0 mid=1 down=1
[LED] state=off reason=wake
[LED] state=normal_on reason=awake
[SLEEP] wake source=gpio slept=151825ms led=normal_on

正常1
[INPUT] event=DownShort
[LED] state=sleep_off reason=light_sleep_enter
[SLEEP] enter deadline_in=111000ms led=sleep_off
[SLEEP][CFG] gpio_wakeup_enable up=0(ESP_OK) mid=0(ESP_OK) down=0(ESP_OK) gpio=0(ESP_OK)
[SLEEP][CFG] timer_wakeup=0(ESP_OK) requested_ms=111000
[SLEEP][LL] before_sleep pins up=1 mid=1 down=1
[SLEEP][LL] returned err=0 cause=7 slept_ms=3217
[SLEEP][LL] after_wake pins up=0 mid=1 down=1
[LED] state=off reason=wake
[LED] state=normal_on reason=awake
[SLEEP] wake source=gpio slept=3217ms led=normal_on
[INPUT] event=UpShort
[LED] state=sleep_off reason=light_sleep_enter
[SLEEP] enter deadline_in=106000ms led=sleep_off
[SLEEP][CFG] gpio_wakeup_enable up=0(ESP_OK) mid=0(ESP_OK) down=0(
正常2
[INPUT] event=DownShort
[POWER] peripheral rail=ON
[PHOTO] SD ready
[PHOTO] SD unmounted (power off)
[POWER] peripheral rail=OFF
[PHOTO] next -> index=4/6 reason=key_down source=epd4
[LED] state=breath reason=photo_render
[APP] render begin state=Photo partial=false led=breath
[POWER] peripheral rail=ON
[PHOTO] SD ready
[PHOTO] render epd4 file=【哲风壁纸】动漫-卡通-比奇堡.epd4 index=4
[EPD] busy wait=22356ms (led updated)
[PHOTO] SD unmounted (power off)
[POWER] peripheral rail=OFF
[LED] state=breath reason=render_done
[APP] render done, epd sleep led=breath
[LED] state=sleep_off reason=light_sleep_enter
[SLEEP] enter deadline_in=3572756ms led=sleep_off
[SLEEP][CFG] gpio_wakeup_enable up=0(ESP_OK) mid=0(ESP_OK) down=0(ESP_OK) gpio=0(ESP_OK)
[SLEEP][CFG] timer_wakeup=0(ESP_OK) requested_ms=3572756
[SLEEP][LL] before_sleep pins up=1 mid=1 down=1
[SLEEP][LL] returned err=0 cause=7 slept_ms=25412
[SLEEP][LL] after_wake pins up=0 mid=1 down=1
[LED] state=off reason=wake
[LED] state=normal_on reason=awake
[SLEEP] wake source=gpio slept=25412ms led=normal_on
[INPUT] event=UpShort
[POWER] peripheral rail=ON
[PHOTO] SD ready
[PHOTO] SD unmounted (power off)
[POWER] peripheral rail=OFF
[PHOTO] prev -> index=3/6 reason=key_up source=epd4
[LED] state=breath reason=photo_render
[APP] render begin state=Photo partial=false led=breath
[POWER] peripheral rail=ON
[PHOTO] SD ready
[PHOTO] render epd4 file=【哲风壁纸】全家福-动漫.epd4 index=3

此外，由于日历页下上键下键无功能，因此不需要唤醒，只有中键短按、长按可以唤醒，也不需要配日志

2 日志修改
每次用户一次操作出现的日志前后应该有三行回车以区分不同操作，包括每次自动操作，日志也应该有三行回车以区分。

日志级别修改： 当前日志过于详细，仅用于debug，后续需要修改日志级别，减少不必要的日志输出。比如STA扫描及其相关日志，照片切换的详细信息，一般来说，一次操作只需要显示用户的操作，比如检测到了下键，对应的过程（比如唤醒、开始切换照片，LED灯变化，照片切换完成，消耗时间xx ms，进入睡眠），即从唤醒到睡眠发生的中间变化的日志

睡眠前的日志总是显示不完整
[SLEEP] enter deadline_in=106000ms led=sleep_off
[SLEEP][CFG] gpio_wakeup_enable up=0(ESP_OK) mid=0(ESP_OK) down=0(

3 灯光

每次全刷时扫描STA结果出现前灯光都是灭的，实际上这时已经唤醒了，已经强调多次了。全刷前的预同步灯光就应该保持呼吸状态，尽管日志已经是直接开始呼吸，我根据我的观察，实际上是先闪一下，然后熄灭，直到出现STA扫描的日志结果灯光才开始真正呼吸，结合STA日志问题一起修

每次STAscan时灯光都是没有任何信号的，应该在扫描开始时进入全刷的呼吸状态，或者配置的状态，也就是同步连接STA都是其他状态的一部分，不是单独的灯光状态

[INPUT] event=MidShort
[LED] state=double_blink reason=page_switch
[CAL] heap before_alloc free=189256 largest=110580
[CAL] framebuffer alloc failed bytes=192000 reason=switch_to_calendar
[CAL] heap alloc_failed free=189256 largest=110580
[APP] page switch -> Calendar
[LED] state=breath reason=calendar_render
[TIME] pre-refresh time sync required
[WIFI][EVT] WIFI_READY (0)
[WIFI][EVT] STA_START
[WIFI] STA scan begin
[WIFI][EVT] WIFI_SCAN_DONE (1)
[WIFI] STA scan found=70
[WIFI]   - #1 ssid= rssi=-47 ch=1 enc=4
[WIFI]   - #2 ssid=web.wlan.bjtu rssi=-64 ch=13 enc=0
[WIFI]   * #3 ssid=phone.wlan.bjtu rssi=-64 ch=13 enc=5
[WIFI]   - #4 ssid=宸 rssi=-73 ch=11 enc=4
[WIFI]   * #5 ssid=phone.wlan.bjtu rssi=-75 ch=1 enc=5
[WIFI]   - #6 ssid=京A00001 rssi=-75 ch=6 enc=4
[WIFI]   - #7 ssid= rssi=-76 ch=6 enc=4
[WIFI]   - #8 ssid=web.wlan.bjtu rssi=-77 ch=1 enc=0
[WIFI]   * #9 ssid=phone.wlan.bjtu rssi=-78 ch=9 enc=5
[WIFI]   - #10 ssid=web.wlan.bjtu rssi=-79 ch=9 enc=0
[WIFI]   * #11 ssid=phone.wlan.bjtu rssi=-80 ch=9 enc=5
[WIFI]   - #12 ssid=web.wlan.bjtu rssi=-81 ch=9 enc=0
[WIFI]   - #13 ssid=Guest_Web rssi=-82 ch=1 enc=0
[WIFI]   - #14 ssid=Teacher_2X rssi=-82 ch=1 enc=4
[WIFI]   - #15 ssid=Guest_Web rssi=-82 ch=1 enc=0
[WIFI]   - #16 ssid=Teacher_2X rssi=-82 ch=1 enc=4
[WIFI]   - #17 ssid=HUAWEI-10H8EE rssi=-82 ch=11 enc=3
[WIFI]   - #18 ssid= rssi=-83 ch=1 enc=4
[WIFI]   * #19 ssid=phone.wlan.bjtu rssi=-83 ch=13 enc=5
[WIFI]   - #20 ssid=web.wlan.bjtu rssi=-84 ch=13 enc=0
[WIFI]   - #21 ssid= rssi=-85 ch=6 enc=3
[WIFI]   - #22 ssid=一爷配俩孙 rssi=-85 ch=10 enc=4
[WIFI]   - #23 ssid=wifi.wlan.bjtu rssi=-86 ch=1 enc=4
[WIFI]   - #24 ssid=vvvbbb rssi=-86 ch=1 enc=3
[WIFI]   - #25 ssid= rssi=-86 ch=6 enc=3
[WIFI]   - #26 ssid= rssi=-86 ch=6 enc=3
[WIFI]   - #27 ssid=CMCC-Y9uT rssi=-86 ch=11 enc=3
[WIFI]   - #28 ssid=HONOR 100 rssi=-87 ch=1 enc=3
[WIFI]   - #29 ssid=马家宿舍-2.4 rssi=-87 ch=6 enc=4
[WIFI]   - #30 ssid=CMCC-YYcd rssi=-87 ch=6 enc=4
[WIFI]   - #31 ssid=ZZ2003 rssi=-87 ch=6 enc=4
[WIFI]   - #32 ssid=CU_fcPf rssi=-87 ch=8 enc=4
[WIFI]   - #33 ssid=web.wlan.bjtu rssi=-87 ch=9 enc=0
[WIFI]   - #34 ssid= rssi=-88 ch=1 enc=3
[WIFI]   - #35 ssid=Xiaomi_DE99 rssi=-88 ch=2 enc=4
[WIFI]   - #36 ssid=ST rssi=-88 ch=6 enc=3
[WIFI]   - #37 ssid=Emily-Frank rssi=-88 ch=6 enc=3
[WIFI]   - #38 ssid=web.wlan.bjtu rssi=-88 ch=9 enc=0
[WIFI]   * #39 ssid=phone.wlan.bjtu rssi=-88 ch=9 enc=5
[WIFI]   * #40 ssid=phone.wlan.bjtu rssi=-88 ch=9 enc=5
[WIFI]   - #41 ssid=Ruby702 rssi=-89 ch=1 enc=3
[WIFI]   - #42 ssid= rssi=-89 ch=10 enc=4
[WIFI]   - #43 ssid=web.wlan.bjtu rssi=-90 ch=5 enc=0
[WIFI]   - #44 ssid= rssi=-90 ch=6 enc=4
[WIFI]   - #45 ssid= rssi=-90 ch=11 enc=3
[WIFI]   - #46 ssid= rssi=-91 ch=1 enc=3
[WIFI]   - #47 ssid=STB_9bMa rssi=-91 ch=3 enc=3
[WIFI]   * #48 ssid=phone.wlan.bjtu rssi=-91 ch=5 enc=5
[WIFI]   - #49 ssid=TP-LINK_076A rssi=-91 ch=6 enc=4
[WIFI]   - #50 ssid=CU_XYZ rssi=-91 ch=8 enc=3
[WIFI]   - #51 ssid=CU-483D rssi=-91 ch=10 enc=4
[WIFI]   - #52 ssid=VR rssi=-91 ch=11 enc=4
[WIFI]   - #53 ssid=caoxi2024 rssi=-92 ch=1 enc=4
[WIFI]   - #54 ssid= rssi=-92 ch=6 enc=3
[WIFI]   - #55 ssid=TP-LINK_D0BD rssi=-92 ch=11 enc=4
[WIFI]   - #56 ssid= rssi=-93 ch=1 enc=3
[WIFI]   - #57 ssid=2303 rssi=-93 ch=3 enc=4
[WIFI]   - #58 ssid= rssi=-93 ch=9 enc=3
[WIFI]   - #59 ssid=MagicWifi rssi=-93 ch=9 enc=3
[WIFI]   - #60 ssid=GUEST 1502 rssi=-93 ch=11 enc=3
[WIFI]   - #61 ssid=yi301 rssi=-94 ch=1 enc=4
[WIFI]   - #62 ssid= rssi=-94 ch=6 enc=4
[WIFI]   - #63 ssid=Cudy-C006 rssi=-94 ch=9 enc=4
[WIFI]   - #64 ssid= rssi=-94 ch=11 enc=0
[WIFI]   - #65 ssid=cheng rssi=-94 ch=11 enc=4
[WIFI]   * #66 ssid=phone.wlan.bjtu rssi=-94 ch=13 enc=5
[WIFI]   - #67 ssid= rssi=-95 ch=11 enc=4
[WIFI]   - #68 ssid=cheng_Wi-Fi5 rssi=-95 ch=11 enc=4
[WIFI]   - #69 ssid= rssi=-95 ch=11 enc=3
[WIFI]   * #70 ssid=phone.wlan.bjtu rssi=-98 ch=13 enc=5
[WIFI] enterprise cert time check=enabled
[WIFI] STA enterprise begin user=23120829
[WIFI] STA connecting reason=calendar_pre_refresh ssid=phone.wlan.bjtu auth=enterprise user_len=8 pass_len=8 timeout=8s initial_status=WL_DISCONNECTED/6
[CAL] pre-refresh sync: starting STA before full refresh
[WIFI] STA connect timeout -> stop (last_status=WL_DISCONNECTED/6)
[WIFI] stop reason=sta_connect_timeout
[WIFI][EVT] STA_DISCONNECTED reason=8 (ASSOC_LEAVE) ssid=phone.wlan.bjtu
[WIFI][EVT] STA_STOP
[APP] WiFi exited without saved settings reason=auto_exit
[CAL] pre-refresh sync settled -> proceed render wifi_seen=false
[APP] render begin state=Calendar partial=false led=breath
[POWER] peripheral rail=ON
[WIFI][EVT] STA_LOST_IP
[EPD] busy wait=22346ms (led updated)
[CAL] full refresh layout=landscape_split reason=forced_full
[POWER] peripheral rail=OFF
[LED] state=breath reason=render_done
[APP] render done, epd sleep led=breath
[LED] state=sleep_off reason=light_sleep_enter
[SLEEP] enter deadline_in=8999ms led=sleep_off
[SLEEP][CFG] gpio_wakeup_enable up=0(ESP_OK) mid=0(ESP_OK) down=0(ESP_OK) gpio=0(ESP_OK)
[SLEEP][CFG] timer_wakeup=0(ESP_OK) requested_ms=9000
[SLEEP][LL] before_sleep pins up=1 mid=1 down=1
[SLEEP][LL] returned err=0 cause=4 slept_ms=9000
[SLEEP][LL] after_wake pins up=1 mid=1 down=1
[LED] state=off reason=wake
[LED] state=normal_on reason=awake
[SLEEP] wake source=timer_or_other slept=9000ms led=normal_on
[CAL] time slot changed key=1167176830 interval=600s -> header refresh
[LED] state=breath reason=calendar_render
[APP] render begin state=Calendar partial=true led=breath
[POWER] peripheral rail=ON
[CAL] header partial reason=time_tick regions=1 last_area=(64,40,96,36) elapsed=22516ms
[POWER] peripheral rail=OFF
[LED] state=breath reason=render_done
[APP] render done, epd sleep led=breath
[LED] state=sleep_off reason=light_sleep_enter
[SLEEP] enter deadline_in=577000ms led=sleep_off
[SLEEP][CFG] gpio_wakeup_enable up=0(ESP_OK) mid=0(ESP_OK) down=0(E


4 STA模式

长按中键进入配置模式，灯光闪烁，此时都是正常的，然后按下键进入STA模式，此时灯光突然熄灭，几秒后进入常亮状态

应当是，按下中键时灯光亮起，达到长按的时间要求后变为快速闪烁（单闪），此时代表配置模式，之后用户选择AP或者STA，选择后进入双闪状态（一定要和单闪区分开），代表正在连接，连接成功后（对于STA是成功连上网络，对于AP是用户连到配置网络，AP模式下连接STA不会改变灯光状态，直到用户连到配置网络才会变）进入常亮状态，连接失败则回到配置模式（单闪），等待用户重新选择

STA模式连接成功，同步退出后和AP模式下一样应该执行一次全刷

5 刷新间隔问题

现在全刷间隔设置的明明是一小时，结果却在下午三点进行一次全刷后再也没全刷过，全是局刷，找到问题根源并修复

日历有全刷和局刷，全刷后局刷的间隔应该重置，比如局刷十分钟，全刷一小时，则每局刷五次，第六次局刷被全刷代替


6 其他

全刷后边突然出现刷出无网络状态和空电池，接入type C后突然自动执行了sta同步和全刷，刷出正常联网状态和满电池

