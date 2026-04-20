# Refactor Progress

更新时间：2026-04-17

## 用途

本文件持续记录：

- 已完成的重构块
- 当前正在推进的重构块
- 每个稳定阶段已经做过的自动化验证
- 后续仍待完成的结构性缺口

规则：

- 每完成一段“可编译、可测试”的稳定改动，就更新本文件
- 本文件记录进展，不替代最终真机验收
- 真机验收范围统一维护在 `docs/REFACTOR_REGRESSION_CHECKLIST.md`

## 当前总体状态

四个缺口里，当前进度如下：

- 测试护栏：已建立第一批
- `WifiManager` 解耦：进行中，已完成前半段
- 日历脏区化局刷：尚未开始主路径切换
- MCU 级 `light/deep sleep`：`light sleep` 设计已完成，代码实现未开始

## 已完成

### 1. 基线与记录

- 新增 `docs/BASELINE.md`
- 新增 `docs/TEST_STRATEGY.md`
- 新增 `docs/REFACTOR_REGRESSION_CHECKLIST.md`

### 2. 宿主机逻辑测试入口

- 新增 `test/run_logic_tests.sh`
- 新增 `test/logic/test_logic.cpp`
- 新增 `test/support/Arduino.h`

当前宿主机测试已覆盖：

- `RefreshPolicy`
- `PartialRefreshGeometry`
- `CalendarLogic`
- `CalendarSettings`
- `CalendarEventNormalize`
- `CalendarStore`
- `CalendarSyncService`
- `CalendarIcsCore` 第一批 helper

### 3. 已抽离的模块

- `src/app/RefreshPolicy.*`
- `src/display/PartialRefreshGeometry.*`
- `src/calendar/CalendarLogic.*`
- `src/system/CalendarSettings.*`
- `src/system/SettingsStore.*`
- `src/system/CalendarData.h`
- `src/system/CalendarStore.*`
- `src/system/CalendarSyncService.*`
- `src/system/CalendarEventNormalize.*`
- `src/system/CalendarIcsCore.*`

### 4. 主路径已接回新模块

- `App.cpp` 已复用刷新调度 helper
- `PartialRefresh.cpp` 已复用局刷几何 helper
- `CalendarModel.cpp` 已复用日历筛选和 lane helper
- `WifiManager.cpp` 已复用设置、事件存储、同步合并、事件归一化和部分 ICS helper

## 当前正在做

当前焦点是继续缩小 `WifiManager.cpp` 中的 ICS 解析/展开逻辑。

已经迁出的 ICS helper 包括：

- `parseDigits`
- `paramsContainDateValue`
- `icsUnescape`
- `appendUnfoldedIcsLines`
- `splitIcsProperty`
- `parseRruleCore`
- `parseIcsDateTime`
- `parseIcsDateField`
- `weekdayMon0FromTm`
- `localWeekWindowStart`
- `localWindowEndOneMonth`
- `eventOverlapsWindow`
- `trimDisplayField`
- `buildImportedTitle`
- `weekdayMaskBit`
- `parseByDayToken`

仍在 `WifiManager.cpp` 的主要剩余部分：

- `ParsedIcsEvent` / `IcsOccurrenceKey` / `IcsOverride` / `ImportedCalendarEvent`
- `parseIcsEventFromLines`
- recurrence 展开与 override 合并
- `parseIcsBodyIntoEvents`
- `collectOverrideMetadata`
- `expandSingleEvent`
- `expandRecurringEvent`
- `appendOverrideEvents`

## 2026-04-16 当日新增进展

- 已把 `IcsOccurrenceKey`、`ParsedIcsEvent`、`buildOccurrenceKey`、`occurrenceKeysEqual`、`hasOccurrenceKey`、`parseIcsEventFromLines` 迁入 `CalendarIcsCore`
- `WifiManager.cpp` 已删除这部分重复定义
- `WifiManager.cpp` 中 `RRULE` 解析已直接复用 `parseRruleCore`
- 宿主机测试已新增对 `parseIcsEventFromLines` 和 occurrence key helper 的覆盖
- 已把 `parseIcsBodyIntoEvents`、`collectOverrideMetadata`、`expandSingleEvent`、`expandRecurringEvent`、`appendOverrideEvents`、`sortImportedEvents` 迁入 `CalendarIcsCore`
- `WifiManager.cpp` 里的 ICS 展开流程已改成直接调用 `CalendarIcsCore`
- 宿主机测试已新增对 `VEVENT body` 解析、override 收集、weekly recurrence 展开的覆盖
- 已把“导入结果归一化”从 `WifiManager.cpp` 迁入 `CalendarSyncService`
- `WifiManager.cpp` 现在对 imported items 只负责拿结果和调用 service，不再逐条改写字段
- 宿主机测试已新增对 imported event 归一化的覆盖
- 已删除 `WifiManager` 中 `deserializeCalendarEvents` 和一组 `normalizeCalendar*` 空转 wrapper
- `WifiManager` 现在直接调用 `CalendarStore` / `CalendarEventNormalize` / `CalendarSyncService` 的正式接口
- 这一轮的目标是把 `WifiManager` 收到“协调器 + Portal/网络入口”形态，这部分已达成阶段性完成

## 最近一次稳定验证

已执行：

- `./test/run_logic_tests.sh`
- `/home/deigel/.platformio/penv/bin/pio run -e esp32dev`

结果：

- 宿主机逻辑测试通过
- `esp32dev` 编译通过

最近一次记录的编译体积：

- RAM：`17.6%`
- Flash：约 `33.7%`

## 当前最新验证

- `./test/run_logic_tests.sh`：通过
- `pio run -e esp32dev`：通过
- 当前编译体积：RAM `17.6%`，Flash `33.7%`

## 2026-04-17 `light sleep` 设计

- 新增 `docs/LIGHT_SLEEP_DESIGN.md`
- 已按当前代码结构明确：
  - `Normal + Photo` 的 `light sleep` 入口
  - `Normal + Calendar` 的 `light sleep` 入口
  - `ConfigWait / ConfigAP / ConfigSTA` 第一版不纳入 `light sleep`
  - WiFi/AP/STA/日历同步期间第一版禁止进入 `light sleep`
- 已明确第一版唤醒源策略：
  - RTC 定时器
  - `Up / Mid / Down` 按键
  - `GPIO0(Up)` 的启动绑定位风险通过真机验收覆盖，而不是在设计上排除
- 已明确第一版实现顺序：
  - `LightSleepController`
  - `App` 的可睡判定与 deadline 计算
  - `WifiManager` 的 sleep block 判定
  - `main.cpp` 主循环改造

## 2026-04-17 当日新增进展

- 新增 `src/app/CalendarRefreshPlanner.*`
- 日历刷新决策已从 `App.cpp` 的内联条件分支抽成独立 planner
- planner 当前已能表达三类路径：
  - 强制全刷 / 跨天 / partial budget 超限
  - 时间头部局刷
  - 现有 framebuffer 兼容局刷
- `App::renderCalendarPage()` 已改成按 `CalendarRefreshPlan` 执行刷新
- 宿主机测试已新增对 planner 决策分支的覆盖
- planner 已加入 dirty region kind，执行层不再猜测 rect 的含义
- header 局刷已从“只支持时间”扩展为：
  - 时间
  - 天气
  - 传感器
- `App` 现在会比较前后 `CalendarModel`，在“body 未变化、仅 header 字段变化”时走多区域 header partial
- 目前的 body 等价判定故意忽略了当前时间 marker 的移动，保持与旧版时间头部局刷一致的行为边界

## 2026-04-17 策略收束

按当前产品取舍，已将 header 局刷策略收束为：

- 只保留 `header_time` 独立局刷
- `header_weather` / `header_sensors` 不再作为独立局刷触发目标
- 天气和温湿度继续跟随全刷或兼容刷新更新

原因是：

- 时间是高价值、可感知的频繁变化项
- 天气/温湿度是慢变量，单独局刷收益不足以覆盖复杂度和测试成本

这一步的定位是“先把刷新决策结构化”，不是“一次性完成脏区化”。
现阶段仍保留原有兼容路径：

- 时间变化：头部时间局刷
- 其它非全刷情况：仍走整页 framebuffer 兼容局刷

所以下一刀如果继续推进，应该是：

1. 把 header 区进一步拆成时间 / 天气 / 传感器几个脏区
2. 再把 schedule panel 引入 planner 的 dirty region 集
3. 最后才考虑削弱整页 framebuffer 依赖

## 当前拆分后的状态

`WifiManager.cpp` 里与 ICS 相关的内容，已经从“自己负责 helper + 事件解析 + recurrence 展开”缩减为：

- 负责发起同步
- 负责记录同步统计、日志和错误状态
- 负责对导入结果做最终归一化并交给 `CalendarSyncService` 合并

已经不再直接持有这几块实现细节：

- `VEVENT` 行级解析
- `VEVENT body` 拆解
- override metadata 收集
- single / recurring occurrence 展开
- imported item 排序

## 本阶段收尾判断

以“`WifiManager` 解耦”这个阶段为边界，当前可以认为已经阶段性完成：

- 设置持久化已归入 `SettingsStore`
- 日历事件存储已归入 `CalendarStore`
- ICS 解析和 recurrence 展开已归入 `CalendarIcsCore`
- 导入结果归一化与合并已归入 `CalendarSyncService`
- `WifiManager` 自身不再保留这几类薄包装接口

当前剩下仍然在 `WifiManager` 中的内容，属于本阶段刻意保留：

- WiFi AP/STA 生命周期
- Portal HTTP 路由与请求处理
- 文件上传/下载/目录操作
- 天气与 geocode 请求编排
- 传感器轮询与电池读取

这些不再归入“本轮必须继续拆掉”的范围，下一阶段应转向日历脏区化，而不是继续无限拆 `WifiManager`。

## 下一步

按当前计划，下一阶段继续做：

1. 继续把 ICS 解析和 recurrence 展开 helper 从 `WifiManager.cpp` 往 `CalendarIcsCore` 收拢
2. 保持宿主机测试和 `esp32dev` 编译持续通过
3. ICS 链路收束到足够程度后，再进入日历脏区化重构
