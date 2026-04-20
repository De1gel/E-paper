# Baseline

更新时间：2026-04-15

## 当前基线

本文件冻结“开始系统化重构前”的可运行状态，用于后续每个阶段回归对比。

## 当前能力

- 启动后进入默认页面，当前调试默认页为 `Calendar`
- `Photo` 页可扫描 SD 卡 `/pic/*.epd4` 并全屏显示
- 中键可在 `Photo / Calendar` 之间切换
- 上下键可在 `Photo` 页切换图片
- 长按中键进入配置模式
- 可启动 AP Portal / STA Portal
- 设置可持久化保存
- 手动日程支持 CRUD
- `calendar_url` 支持本地 SPIFFS 路径和 HTTP(S) ICS 导入
- 日历页支持整页条带全刷
- 日历页支持头部时间局刷
- 天气测试、地理解析、联网校时接口已接入

## 关键默认值

- 默认构建环境：`esp32dev`
- 文件系统：`spiffs`
- Flash：`16MB`
- `calendar_refresh_sec` 默认 `900`
- `calendar_time_refresh_sec` 默认 `600`
- 默认天气城市：`Shanghai`
- 当前调试默认页：`Calendar`

## 当前限制

- 尚未实现 MCU 级 `light sleep / deep sleep`
- 日历局刷仍依赖整屏 framebuffer 兼容路径
- `WifiManager` 仍承担 WiFi、Portal、设置、日历同步、传感器等多职责
- 自动化测试体系尚未建成，主要依赖日志与手工验证

## 最小回归清单

- 编译 `esp32dev` 通过
- SPIFFS 资源上传后 Portal 首页可访问
- `Photo` 页可显示 `/pic` 中有效 `.epd4`
- 中键切换到 `Calendar` 后可正常渲染
- 手动新增一条日程后日历页可见
- 本地样例 ICS 可导入并显示事件
- 时间刷新到下一个 bucket 时头部局刷仍触发
- AP / STA Portal 可以进入与退出

## 关键日志观察点

- `[BOOT] setup begin`
- `[APP] begin`
- `[PHOTO] epd4 scan count=...`
- `[CAL] full refresh layout=...`
- `[CAL] header time partial area=...`
- `[CALSYNC] fetch begin ...`
- `[CALSYNC] expanded imported=...`
