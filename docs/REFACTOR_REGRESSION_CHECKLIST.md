# Refactor Regression Checklist

更新时间：2026-04-21

## 用途

本文件用于持续记录：

- 当前重构已经影响到的功能面
- 后续重构新增影响的功能面
- 最终需要在真机上逐项验收的项目

规则：

- 后续每次重构只要可能影响用户可见行为，就更新本文件
- 不因为宿主机测试通过而从本文件移除真机验收项
- 只有你最终完成真机验收后，才把对应项改为“已验证”

状态说明：

- `待验证`：尚未做最终真机验收
- `已验证`：你已完成真机测试确认
- `新增关注`：最近重构新引入的重点关注项

## 当前重构影响范围

截至目前，以下模块已经发生结构性调整或 helper 抽离：

- `SettingsStore`
- `CalendarStore`
- `CalendarSyncService`（第一轮）
- `CalendarEventNormalize`
- `CalendarIcsCore`（第一轮）
- `RefreshPolicy`
- `PartialRefreshGeometry`
- `CalendarLogic`
- `test/run_logic_tests.sh` 宿主机测试入口

最近新增的影响面：

- ICS 解析中的 `parseIcsDateTime / parseIcsDateField / parseRruleCore / imported title` 等 helper 已迁入 `CalendarIcsCore`
- `WifiManager` 仍在继续拆分，后续每次只要影响到 ICS 导入、Portal、设置保存、页面刷新路径，都继续保留在本清单中
- `light sleep` 主循环已接入，`Normal + Photo / Calendar` 的等待期不再固定 `delay(50)` 空转

因此，下面这些功能都需要纳入最终真机回归。

## 编译与启动

- `已验证`：`esp32dev` 固件可正常烧录并启动
- `已验证`：串口启动日志正常，能看到 `[BOOT] setup begin`、`[APP] begin`
- `已验证`：默认启动页行为符合当前预期（当前调试状态为 `Calendar`）

## 页面与按键

- `已验证`：中键短按可在 `Photo / Calendar` 间切换
- `已验证`：`Photo` 页上键切上一张、下键切下一张
- `已验证`：中键长按可进入配置模式
- `已验证`：配置模式下按键行为仍正确

## Photo 页

- `已验证`：SD 卡 `/pic` 扫描仍正常
- `已验证`：有效 `.epd4` 文件仍能正常显示
- `待验证`：无有效图片时仍能走清屏 fallback
- `已验证`：照片自动轮播间隔仍符合配置

## Calendar 页渲染

- `已验证`：切到 `Calendar` 页后可完整渲染
- `待验证`：月历网格、当天标记、星期标题显示正常
- `待验证`：日程列表顺序、lane 排布、时间轴显示正常
- `待验证`：顶部日期/时间/天气/温湿度显示正常
- `待验证`：语言切换后中英法文本仍正常

## Calendar 刷新策略

- `已验证`：整页条带全刷仍正常
- `已验证`：头部时间局刷仍正常触发
- `待验证`：`calendar_time_refresh_sec` 为 `600/1200/1800/3600/0` 时行为正确
- `待验证`：跨天时仍会触发完整日历刷新
- `新增关注`：最近已调整 `RefreshPolicy` / `PartialRefreshGeometry` / 头部时间刷新相关 helper
- `新增关注`：刷新决策现已改由 `CalendarRefreshPlanner` 统一规划
- `待验证`：partial budget 达到阈值后仍会回落到整页全刷
- `待验证`：普通非时间变化刷新仍保持当前兼容局刷行为，不出现区域错刷
- `新增关注`：header 局刷策略已收束为“仅时间”，天气和温湿度不再单独局刷
- `待验证`：天气/温湿度变化时不会误触发额外 header partial，而是继续跟随全刷或兼容刷新

## 手动日程 CRUD

- `待验证`：Portal 读取日程列表正常
- `待验证`：新增手动日程成功后，列表和日历页都能看到
- `待验证`：删除手动日程后，列表和日历页同步更新
- `待验证`：`once / daily / weekly` 三类规则仍正常
- `新增关注`：最近已重构 `CalendarStore` 和事件字段归一化逻辑

## ICS / Calendar Sync

- `待验证`：本地 SPIFFS ICS 文件导入仍正常
- `待验证`：HTTP(S) ICS 导入仍正常
- `待验证`：导入后手动事件不会被错误覆盖
- `待验证`：已有 ICS 事件会按 `external_id` 正确更新而不是重复新增
- `待验证`：`RRULE` 的 `FREQ/INTERVAL/COUNT/BYDAY/UNTIL` 当前支持行为仍正常
- `待验证`：导入后日历页可见事件与 Portal 数据一致
- `新增关注`：最近已重构 `CalendarSyncService`、`CalendarIcsCore`、`CalendarStore`

## 设置持久化

- `待验证`：WiFi、语言、天气、日历相关设置保存后重启仍生效
- `待验证`：`calendar_time_refresh_sec` 保存/读取仍正常
- `待验证`：`calendar_url` 保存/读取仍正常
- `待验证`：默认值回填逻辑仍正确
- `新增关注`：最近已重构 `SettingsStore`

## Portal / API

- `待验证`：SPIFFS 页面资源上传后 Portal 首页可访问
- `已验证`：`/api/settings` 读写正常
- `待验证`：`/api/calendar/events` 的 GET/POST/DELETE 正常
- `待验证`：`/api/weather_test` 正常
- `待验证`：`/api/geocode` 正常
- `待验证`：Portal 文件管理仍正常

## 联网与时间

- `已验证`：STA 连接流程正常
- `已验证`：天气接口校时仍正常
- `待验证`：时区更新后本地时间显示正确
- `待验证`：校时后不会引发异常重复刷新

## `light sleep`

- `新增关注`：`main.cpp` 主循环已接入 `light sleep`
- `新增关注`：第一版仅 `Normal + Photo / Calendar` 启用 `light sleep`
- `新增关注`：`ConfigWait / ConfigAP / ConfigSTA` 第一版不进入 `light sleep`
- `新增关注`：`WifiManager` 非空闲时第一版禁止进入 `light sleep`
- `已验证`：`Photo` 页静置时会进入 `light sleep`
- `已验证`：`Photo` 页到自动轮播时间会被 RTC 唤醒并换图
- `已验证`：`Calendar` 页静置时会进入 `light sleep`
- `已验证`：`Calendar` 页到下一次时间头刷新桶边界会被 RTC 唤醒并刷新时间
- `待验证`：`Calendar` 页到下一次周期全刷检查点会被 RTC 唤醒并按原策略刷新
- `已验证`：`Up / Mid / Down` 三个按键都能从 `light sleep` 唤醒
- `已验证`：按键唤醒后不会立刻再次睡回去，能正常响应后续按键动作
- `已验证`：`ConfigWait` 中不会进入 `light sleep`
- `已验证`：AP Portal 打开期间不会进入 `light sleep`
- `已验证`：STA 连接中、STA 运行中、日历同步中不会进入 `light sleep`
- `已验证`：按住 `Up(GPIO0)` 手动复位时启动行为仍可接受
- `待验证`：烧录后自动重启时，`GPIO0(Up)` 不会引入不可接受的启动异常

## 低层显示与稳定性

- `待验证`：局刷区域无明显错位、斜行、窗口裁剪异常
- `待验证`：整页刷新后无明显新增显示异常
- `待验证`：上传 SPIFFS / 配置保存 / 联网同步后不会触发异常额外全刷

## 指示灯与睡眠联动

- `新增关注`：睡眠期间 LED 必须始终熄灭
- `新增关注`：非睡眠期间 LED 必须始终有明确状态
- `已验证`：`Photo` 页切图时 LED 为呼吸态，并在渲染结束前持续可见
- `已验证`：`Calendar` 页刷新时 LED 为呼吸态，并在渲染结束前持续可见
- `已验证`：页面/模式切换时 LED 会触发双闪
- `已验证`：`ConfigWait` 为快闪
- `已验证`：`ConfigAP / ConfigSTA` 会话期间为常亮
- `已验证`：`ConfigSTA` 成功连上 WiFi 后会额外闪烁一次
- `已验证`：进入 `light sleep` 前 LED 会熄灭
- `已验证`：从 `light sleep` 唤醒后 LED 会恢复到当前模式对应状态
- `已验证`：串口日志会输出 `[LED]` 状态变化，且与实际灯态一致
- `已验证`：串口中的 `[SLEEP]` / `[APP] render` 日志会带当前 LED 状态

## 当前无需从清单移除的原因

虽然当前已经多轮通过：

- `./test/run_logic_tests.sh`
- `pio run -e esp32dev`

但这些只能说明：

- 纯逻辑 helper 当前通过自动化验证
- 固件当前可编译

还不能替代最终真机验收，所以以上功能项全部继续保留。
