# Test Strategy

更新时间：2026-04-15

## 目标

第一阶段先建立“宿主机可跑的纯逻辑测试”，为后续 `WifiManager` 解耦、日历脏区化和低功耗改造提供护栏。

## 范围

优先覆盖以下逻辑：

- 刷新调度
- 局刷窗口几何对齐
- 日历事件按日筛选
- 时间轴 lane 分配
- `calendar_time_refresh_sec` 归一化

暂不在该阶段覆盖：

- EPD 指令链硬件行为
- WiFi/AP/STA 真实联网
- WebServer 路由集成
- SPIFFS / SD 真机访问

## 测试入口

宿主机逻辑测试入口：

```bash
./test/run_logic_tests.sh
```

该脚本使用本机 `g++` 编译纯逻辑 helper 和测试文件，不依赖 ESP32 板卡。

## 当前测试批次

### Refresh Policy

- `refreshBucketKey`
- `minuteKeyFromTm`
- `sameCalendarMinute`
- `fallbackClockBaseEpoch`

### Partial Refresh Geometry

- `normalizePartialWindow`
- 宽度对齐到 `4px`
- 右边界裁剪
- 空窗口保护

### Calendar Logic

- `calendarEventMatchesToday`
- `assignTimelineLanes`

### Calendar Settings

- `normalizeCalendarTimeRefreshSec`

## 执行要求

- 每次进入下一阶段前先运行宿主机逻辑测试
- 每次涉及渲染、同步或配置重构后，补对应逻辑测试
- 每个阶段结束后再执行一轮手工基线回归

## 后续扩展

下一批建议补充：

- ICS 日期解析
- RRULE 展开
- EXDATE / `RECURRENCE-ID`
- `CalendarModel` 对输入事件的完整输出验证
- 脏区规划与刷新原因判断
