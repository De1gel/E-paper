# 代码模块地图

更新时间：2026-04-10

## 1. 启动与板级初始化

- `src/main.cpp`
  - 初始化 BUSY / RES / DC / CS / PWR_ON
  - 打开 `IO32` 外设供电
  - 初始化 SPI：`SPI.begin(13, 12, 14, 33)`
  - 驱动 `App` 生命周期

## 2. 应用主循环

- `src/app/App.h`
- `src/app/App.cpp`

当前职责：
- 页面状态：`Photo / Calendar`
- 输入事件处理与模式联动
- 照片轮播与 `.epd4` 文件扫描
- 日历页渲染调度
- 显示会话开启 / 关闭

关键现状：
- 照片页走 SD 流式写屏
- 日历页全刷主路径走条带渲染
- 日历局刷兼容路径仍依赖 `calendar_frame_`

## 3. 系统层

### 3.1 输入

- `src/system/InputManager.h`
- `src/system/InputManager.cpp`

职责：
- 三键去抖
- 中键短按 / 长按区分
- 事件队列输出

### 3.2 模式状态机

- `src/system/ModeManager.h`
- `src/system/ModeManager.cpp`

职责：
- `Normal`
- `ConfigWait`
- `ConfigAP`
- `ConfigSTA`

### 3.3 LED 指示

- `src/system/LedManager.h`
- `src/system/LedManager.cpp`

职责：
- 呼吸效果
- 双闪提示
- 配置态常亮 / 快闪 / 连接中闪烁

### 3.4 网络与门户

- `src/system/WifiManager.h`
- `src/system/WifiManager.cpp`

职责：
- AP / STA 生命周期
- WebServer 路由
- Preferences 配置持久化
- Open-Meteo 地理解析与天气校时
- SD 文件 API
- 手动日程 API
- SPIFFS 静态资源服务
- 传感器 / 电量状态采集

## 4. 日历渲染链

### 4.1 数据模型

- `src/calendar/CalendarModel.h`
- `src/calendar/CalendarModel.cpp`

职责：
- 当前日期标题
- 42 格月历数据
- 今日可见事件筛选与排序
- quote 与多语言文案

### 4.2 布局

- `src/calendar/CalendarLayout.h`
- `src/calendar/CalendarLayout.cpp`

职责：
- `LandscapeSplit`
- `PortraitSplit`
- 日历区 / 日程区 / quote 区 / 栅格尺寸计算

### 4.3 场景描述

- `src/calendar/CalendarScene.h`
- `src/calendar/CalendarScene.cpp`

职责：
- 输出矩形、边框、文本、事件 chip 等绘制命令

### 4.4 文本与字形

- `src/calendar/CalendarText.h`
- `src/calendar/CalendarText.cpp`
- `src/fonts/ZhSubsetFont.h`
- `src/fonts/ZhSubsetFont.cpp`
- `src/fonts/ZhSubsetFontData.cpp`

职责：
- ASCII 3x5 字体
- UTF-8 解码
- 中文常用字 16/24 像素字形查找

## 5. 渲染与显示

### 5.1 条带渲染

- `src/render/StripeBuffer.h`
- `src/render/StripeBuffer.cpp`
- `src/render/SceneRasterizer.h`
- `src/render/SceneRasterizer.cpp`

职责：
- 管理条带 packed buffer
- 按条带把 calendar scene 栅格化

### 5.2 面板驱动

- `src/Display_EPD_W21.cpp`
- `src/Display_EPD_W21_spi.cpp`
- `include/Display_EPD_W21.h`
- `include/Display_EPD_W21_spi.h`

职责：
- 面板初始化
- DTM / DRF 写入
- EPD 睡眠
- SPI 数据发送

### 5.3 显示辅助

- `src/display/PartialRefresh.h`
- `src/display/PartialRefresh.cpp`
- `src/display/ColorMap.h`
- `src/display/ColorMap.cpp`

职责：
- 局刷窗口命令链
- 面板颜色映射与 Bayer 4x4 混色辅助

## 6. Portal 前端资源

- `data/portal.html`
- `data/app.js`

职责：
- 设备状态页
- 配置页
- 日程管理页
- 文件管理与上传页
- 浏览器端图片预处理与 `.epd4` 生成

说明：
- 固件优先从 SPIFFS 提供这两个文件
- `data/index.html` 目前不是主入口

## 7. 资源、测试与工具

### 7.1 历史资源

- `src/image.cpp`
- `include/image.h`

说明：
- 这是早期图片数组路径
- 当前主构建已通过 `build_src_filter` 排除 `src/image.cpp`

### 7.2 设计资源

- `assets/charsets/`
- `photo.jpg`
- `设计思路资料/`

### 7.3 测试与实验

- `test/partial_refresh/README.md`
- `test/dither_case_runner.py`
- `test/compare_5algos_3gamma.py`

说明：
- 当前 `test/` 以实验脚本和验证记录为主
- 还没有成体系的固件单元测试

### 7.4 工具脚本

- `tools/fonts/generate_zh_subset_font.py`

职责：
- 生成中文字形子集数据
