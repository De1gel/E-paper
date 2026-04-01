# 代码模块地图

更新时间：2026-04-01

## 1. 入口与主流程
- `src/main.cpp`
  - 板级引脚初始化
  - SPI 初始化
  - 驱动 `App` 生命周期

## 2. 应用层
- `src/app/App.h`
- `src/app/App.cpp`
  - 页面轮播
  - 按键处理（上/下）
  - 渲染调度与日志

## 3. 显示驱动层
- `src/Display_EPD_W21.cpp/.h`
- `src/Display_EPD_W21_spi.cpp/.h`
  - EPD 命令与刷新流程
  - SPI 数据写入

## 4. 显示策略辅助层
- `src/display/ColorMap.h/.cpp`
  - 色彩映射与像素打包
- `src/display/PartialRefresh.h/.cpp`
  - 局部窗口刷新命令链（实验能力）

## 5. 资源与头文件
- `src/image.cpp`
- `include/image.h`

## 6. 测试与文档
- `test/partial_refresh/README.md`
- `docs/PROJECT_HANDOVER.md`
- `docs/ESP32S_DRIVER_BOARD_INFO.md`
- `docs/DESIGN_ASSETS_INDEX.md`
- 本文件及同目录笔记文件
