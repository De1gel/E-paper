# 7.3" E6 墨水屏相框项目现状与交接说明

更新时间：2026-04-10  
本文按当前工作区代码整理，不再沿用 2026-04-01 早期“点屏 demo”阶段的状态描述。  
项目目录：`D:\MyCode\ESP\E-paper`  
驱动板硬件详表：`docs/ESP32S_DRIVER_BOARD_INFO.md`

## 1. 当前完成度

### P1：本地可用版

当前代码已经基本具备 P1 所需主链路：
- 照片页：从 SD 卡 `/pic` 目录读取 `.epd4` 文件并流式写屏
- 日历页：本地时间驱动的月历 + 日程列表 + quote 区域
- 页面切换：中键短按在照片页与日历页之间切换
- 按键控制：照片页支持上一张 / 下一张；长按进入配置模式
- 显示会话：每次渲染前上电，渲染后 `EPD_sleep()` + `IO32 LOW`

### P2：联网数据版

当前代码已具备联网基础设施，但还没有真正接入远端日历同步：
- 已实现 AP / STA WiFi Portal
- 已实现设置持久化、手动日程 CRUD、文件管理、天气接口测试、地理解析、联网校时
- `calendar_url` 仍是保留字段，当前没有拉取远端日历数据的主路径

### P3：优化版

当前代码已有优化基础，但还不是最终形态：
- 已有局刷命令链模块 `src/display/PartialRefresh.*`
- 已有条带全刷路径，降低日历页全刷对大块连续内存的依赖
- 仍未实现 ESP32 自身 `light/deep sleep`
- 日历局刷路径仍依赖整屏 packed framebuffer，尚未完成脏区化

## 2. 当前构建与工程状态

- PlatformIO 环境：`espressif32 + arduino`
- 目标板：`esp32dev`
- Flash 配置：`16MB`
- 分区方案：双 app OTA + SPIFFS
- 文件系统：`spiffs`
- 本地验证：`platformio run --environment esp32dev` 已于 2026-04-10 编译通过

当前构建入口与约束：
- `platformio.ini`：主构建配置
- `partitions_16mb.csv`：16MB 分区表
- `build_src_filter = +<*> -<image.cpp>`：旧的 `src/image.cpp` 示例资源已排除出主构建

## 3. 必须保留的硬件约束

- 屏型：`GDEP073E01(E6)`，分辨率 `800x480`
- 板级关键差异：
  - `CS = GPIO33`
  - `IO32` 拉高后外设上电
  - 当前可用 SPI 初始化：`SPI.begin(13, 12, 14, 33)`

结论：
- 这不是标准微雪参考板
- 后续所有固件开发都必须沿用当前板级适配前提

## 4. 当前运行方式

### 启动与主循环

- `src/main.cpp`
  - 初始化 BUSY / RES / DC / CS / PWR_ON
  - 打开 `IO32`
  - 初始化 SPI
  - 进入 `App::begin()`
- `loop()`
  - 周期调用 `App::update()` 和 `App::render()`
  - 固定 `delay(50)`

### 页面与按键

- 默认进入照片页
- 中键短按：照片页 / 日历页切换
- 上键短按：照片页上一张
- 下键短按：照片页下一张
- 中键长按：进入 `ConfigWait`

### 配置模式

- `ConfigWait`
  - 60 秒无操作自动退出
  - 上键：进入 AP Portal
  - 下键：进入 STA Portal
  - 中键短按：执行白屏操作后返回 Normal
- `ConfigAP`
  - 启动热点 `PhotoFrame_Config`
- `ConfigSTA`
  - 尝试连接已保存的路由器配置

## 5. 当前代码结构

### 业务主链路

- `src/app/App.*`
  - 页面状态
  - 渲染调度
  - 照片轮播
  - 日历页刷新策略

### 系统层

- `src/system/InputManager.*`
  - 按键去抖与事件队列
- `src/system/ModeManager.*`
  - `Normal / ConfigWait / ConfigAP / ConfigSTA`
- `src/system/LedManager.*`
  - 呼吸灯、双闪、配置态指示
- `src/system/WifiManager.*`
  - WiFi、Portal、设置、手动日程、地理解析、天气校时、SD 文件 API、传感器状态

### 日历渲染链

- `src/calendar/CalendarModel.*`
  - 本地日历数据与可见事件整理
- `src/calendar/CalendarLayout.*`
  - 横竖布局与区域计算
- `src/calendar/CalendarScene.*`
  - 将 model + layout 转成绘制命令
- `src/calendar/CalendarText.*`
  - ASCII / UTF-8 / 中文字形适配
- `src/render/StripeBuffer.*`
  - 条带缓冲
- `src/render/SceneRasterizer.*`
  - 条带栅格化

### 显示层

- `src/Display_EPD_W21.cpp`
- `src/Display_EPD_W21_spi.cpp`
- `src/display/PartialRefresh.*`
- `src/display/ColorMap.*`

### Portal 与静态资源

- `data/portal.html`
- `data/app.js`

当前 Portal 主路径优先从 SPIFFS 提供静态文件；若未执行 `uploadfs`，固件会回退到内嵌最小提示页。

### 字体与工具

- `src/fonts/ZhSubsetFont.*`
  - 3000 常用中文字形子集
- `tools/fonts/generate_zh_subset_font.py`
  - 字库生成脚本

## 6. 当前功能边界

### 已实现

- SD 卡 `.epd4` 照片轮播
- 本地日历页
- Portal 配置页 / 状态页 / 文件页
- 手动日程增删改查
- Open-Meteo 地理解析与天气请求测试
- 联网后自动校时并更新时区
- 电量 / 温湿度状态显示（硬件接入时生效）

### 未完成或仅保留接口

- `calendar_url` 未接入远端同步
- `calendar_enabled` 已持久化，但当前没有实际关闭日历页的运行分支
- 没有 `CalendarStore / CalendarSyncService / RefreshPlanner`
- 没有 ESP32 级别休眠状态机
- 日历局刷仍是实验能力，不是主路径

## 7. 已知风险与注意事项

- 不能把原厂闭源固件能力误认为当前源码能力
- 构建配置调整时要同步检查 Flash Size / 分区 / SPIFFS
- Portal 静态资源需要单独 `uploadfs`
- 照片页只识别大小正确的 `.epd4` 文件
- 日历当前虽然已有条带全刷，但仍保留整屏 framebuffer 作为局刷兼容路径
- 测试目录目前以脚本和实验记录为主，不是完整单元测试体系

## 8. 建议的下一阶段优先级

1. 完成日历渲染主路径去 framebuffer 化  
   - 把 `calendar_frame_` 从默认路径剥离，仅保留实验态或完全替换

2. 把手动事件与未来同步事件抽到独立存储层  
   - 降低 `WifiManager` 与日历显示的直接耦合

3. 落实 MCU 级低功耗  
   - 让 `Normal` 真正进入 `light/deep sleep`

4. 明确局刷纳入条件  
   - 在残影、闪烁、稳定性达标前，继续以全刷 / 条带全刷为产品路径

## 9. 配套文档索引

- `docs/CODEBASE_MAP.md`：当前代码模块地图
- `docs/WIFI_PORTAL.md`：当前 Portal 行为与 API
- `docs/WIFI_PORTAL_TEST_PLAN.md`：Portal 测试清单
- `docs/LOW_POWER_POLICY.md`：低功耗目标与当前实现状态
- `docs/CALENDAR_RENDER_REFACTOR_PLAN.md`：日历重构现状与剩余问题
- `docs/REFRESH_STRATEGY_NOTES.md`：全刷 / 快刷 / 局刷策略笔记
- `docs/IMAGE_PIPELINE_NOTES.md`：图片上传与 `.epd4` 处理链路
- `docs/DESIGN_ASSETS_INDEX.md`：资料索引
