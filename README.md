# E-paper

基于 ESP32 的 7.3 寸墨水屏相框/日历固件。目标屏幕为 Good Display GDEP073E01(E6)，分辨率 800x480，当前项目已经包含照片轮播、日历日程页、WiFi 配置门户、天气/校时、文件管理和低功耗相关实验代码。

## 当前功能

- SD 卡照片轮播：读取 `/pic` 目录下的 `.epd4` 文件并写入墨水屏。
- 日历视图：月历、当天时间轴、天气/传感器信息、中文字体子集渲染。
- 手动日程：通过 Web Portal 增删改查日程，支持一次、每日、每周重复。
- WiFi Portal：支持 AP 配置模式和 STA 模式访问，页面资源位于 `data/`。
- 网络配置：支持普通 WiFi、开放网络、企业/校园网 802.1X、网页登录认证字段。
- 天气与时间：Open-Meteo 天气测试、城市地理解析、联网校时，并可同步到 RX8025 RTC。
- 文件管理：通过 Portal 浏览、上传、下载和删除 SD 卡文件。
- 刷新策略：日历页采用条带渲染，保留局部刷新实验路径。

## 硬件目标

- 主控：ESP32 Dev Module
- 屏幕：GDEP073E01(E6)，800x480
- Flash：16MB
- 文件系统：SPIFFS
- 墨水屏 SPI：`SPI.begin(13, 12, 14, 33)`
- 墨水屏 CS：GPIO33
- 外设电源控制：GPIO32
- SD 卡：CS=GPIO5，SCK=18，MISO=19，MOSI=23
- 按键唤醒相关：上键 GPIO0，中键 GPIO35，下键 GPIO34

这块板子的引脚和常见 ESP32 墨水屏示例不完全一致，调整驱动或移植时先看 `docs/ESP32S_DRIVER_BOARD_INFO.md`。

## 构建环境

需要安装 PlatformIO。项目默认环境是 `esp32dev`：

```powershell
platformio run --environment esp32dev
```

如果 `platformio` 不在 PATH 中，也可以使用本机 PlatformIO 虚拟环境里的可执行文件，例如：

```powershell
& 'C:\Users\16952\.platformio\penv\Scripts\platformio.exe' run --environment esp32dev
```

常用命令：

```powershell
platformio run --environment esp32dev
platformio run --environment esp32dev --target upload --upload-port COM4
platformio run --environment esp32dev --target uploadfs --upload-port COM4
platformio device monitor --port COM4 --baud 115200
```

Portal 页面在 `data/` 中，修改后需要执行 `uploadfs`。如果没有上传 SPIFFS，固件会回退到内嵌的简化配置页。

## 使用方式

默认进入照片/日历主界面。按键逻辑：

- 中键短按：照片页和日历页之间切换。
- 上键短按：照片页上一张。
- 下键短按：照片页下一张。
- 中键长按：进入配置等待模式。

配置等待模式中：

- 上键短按：进入 AP Portal。
- 下键短按：进入 STA Portal。
- 中键短按：执行白屏操作并返回正常模式。

AP Portal 默认热点：

- SSID：`PhotoFrame_Config`
- 密码：`12345678`
- 地址：`http://192.168.4.1/`

STA 模式下，设备连接已保存的 WiFi 后，可访问 `http://<device_ip>/` 或尝试 `http://epaper.local/`。

## Web API

主要接口：

- `GET /api/status`：设备状态、电量、传感器、WiFi、时间状态。
- `GET /api/settings`：读取配置。
- `POST /api/settings`：保存 WiFi、语言、日历、天气等配置。
- `GET /api/calendar/events`：读取手动日程。
- `POST /api/calendar/events`：新增或更新手动日程。
- `DELETE /api/calendar/events?id=<id>`：删除手动日程。
- `GET /api/geocode?city=<name>`：城市地理解析。
- `GET /api/weather_test`：测试天气接口并校时。
- `GET /api/files?path=/pic`：列出 SD 卡文件。
- `POST /api/upload?dir=/pic`：上传文件。
- `GET /api/file?path=/pic/xxx.epd4`：下载文件。
- `DELETE /api/file?path=/pic/xxx.epd4`：删除文件。
- `POST /api/dir`：创建目录。
- `POST /api/stop`：停止 Portal。
- `POST /api/reboot`：重启设备。

## 目录结构

- `src/app/`：应用主循环、页面状态、渲染调度、低功耗调度。
- `src/system/`：按键、模式、WiFi Portal、设置存储、日程存储、ICS 同步、传感器状态。
- `src/calendar/`：日历模型、布局、场景命令、文字和字体适配。
- `src/display/`：颜色映射和局部刷新实验代码。
- `src/render/`：条带缓冲与场景栅格化。
- `src/fonts/`：ASCII 字体和中文子集字体数据。
- `data/`：Portal HTML/JS 和示例日历文件。
- `assets/charsets/`：中文字体生成用字符集。
- `tools/fonts/`：中文字体子集生成脚本。
- `docs/`：设计记录、测试计划、硬件说明和交接文档。
- `test/`：逻辑测试和图像/刷新实验脚本。

## 字体生成

中文字体数据由 `tools/fonts/generate_zh_subset_font.py` 生成，默认读取根目录下的 `MSYH.TTC`。字体源文件较大，已在 `.gitignore` 中排除，只提交生成后的 `src/fonts/ZhSubsetFontData.cpp` 和字符集文本。

```powershell
python tools/fonts/generate_zh_subset_font.py
```

## 注意事项

- `calendar_url`、ICS 存储和同步服务已有代码路径，但仍在迭代中，实际行为以当前 Portal 和源码为准。
- 上传图片建议通过 Portal 浏览器端预处理生成 `.epd4`，固件端不做完整图片缩放/裁剪。
- `设计思路资料/`、`.TTC` 字体源文件和本地构建产物不进入版本库。
- 每次改动 `data/portal.html` 或 `data/app.js` 后，除了烧录固件，还要上传文件系统。

## 参考文档

- `docs/PROJECT_HANDOVER.md`：项目现状和交接说明。
- `docs/CODEBASE_MAP.md`：代码模块地图。
- `docs/WIFI_PORTAL.md`：Portal 行为和 API。
- `docs/WIFI_PORTAL_TEST_PLAN.md`：Portal 测试清单。
- `docs/LOW_POWER_POLICY.md`：低功耗策略。
- `docs/CALENDAR_RENDER_REFACTOR_PLAN.md`：日历渲染重构计划。
- `docs/REFRESH_STRATEGY_NOTES.md`：刷新策略记录。
- `docs/IMAGE_PIPELINE_NOTES.md`：图片处理链路。
- `docs/WORKLOG.md`：近期工作记录。
