# `light sleep` 设计

更新时间：2026-04-17

## 1. 目标

本阶段只设计并实现 MCU 级 `light sleep`，不直接进入 `deep sleep`。

目标边界：

- `Normal` 模式下，ESP32 不再固定 `delay(50)` 空转
- 在不影响现有页面逻辑的前提下，让 `Photo` 和 `Calendar` 的等待期进入 `light sleep`
- `ConfigWait / ConfigAP / ConfigSTA` 保持现有交互行为，第一版不进入 `light sleep`
- 保持当前“渲染前上电、渲染后 `EPD_sleep()` + `IO32 LOW`”的外设电源策略

非目标：

- 本阶段不做 `deep sleep`
- 本阶段不改变 `Photo` / `Calendar` 的现有刷新节奏
- 本阶段不改变 `WifiManager` 的 AP/STA/Portal 行为

## 2. 当前代码约束

### 2.1 主循环

当前 [src/main.cpp](/mnt/study/MyCode/ESP/E-paper/src/main.cpp:1) 为固定轮询：

- `loop()` 中持续执行 `g_app.update(now_ms)`
- 然后执行 `g_app.render()`
- 最后固定 `delay(50)`

这意味着：

- MCU 没有进入任何休眠态
- 当前没有“唤醒后恢复”的概念，因为系统一直在运行

### 2.2 外设电源

当前外围电源由 `IO32` 统一控制：

- [src/app/App.cpp](/mnt/study/MyCode/ESP/E-paper/src/app/App.cpp:387) `setPeripheralPower(bool enabled)`
- 显示会话：`beginDisplaySession()` 上电，`endDisplaySession()` 下电
- Photo 页 SD 访问：按需上电/下电
- WiFi 会话：`WifiManager::startAP/startSTA` 拉高，`WifiManager::stop` 拉低

`light sleep` 设计不得绕过这条电源控制链。

### 2.3 运行模式

当前模式来自 [src/system/ModeManager.h](/mnt/study/MyCode/ESP/E-paper/src/system/ModeManager.h:9)：

- `Normal`
- `ConfigWait`
- `ConfigAP`
- `ConfigSTA`

页面状态来自 [src/app/App.h](/mnt/study/MyCode/ESP/E-paper/src/app/App.h:17)：

- `Photo`
- `Calendar`

因此 `light sleep` 第一版必须同时考虑：

- 运行模式
- 页面状态
- WiFi/同步状态
- 是否存在待渲染任务

## 3. 第一版设计原则

### 3.1 只在“明确空闲”时睡眠

第一版只允许在以下条件全部满足时进入 `light sleep`：

- `mode_manager_.mode() == Normal`
- `needs_render_ == false`
- `calendar_pre_refresh_sync_waiting_ == false`
- 没有处于 `beginDisplaySession()` 之后的显示会话
- `WifiManager` 不处于：
  - `StaConnecting`
  - `StaRunning`
  - `ApRunning`
- 没有待处理输入事件

这意味着：

- Portal 期间不睡
- STA 连接和日历同步期间不睡
- 配置模式不睡
- 正在准备全刷或局刷时不睡

### 3.2 唤醒源只做两类

第一版只使用：

- 按键唤醒
- RTC 定时器唤醒

不引入额外外设中断。

### 3.3 唤醒后继续现有主循环

`light sleep` 的定位是“暂停一段时间再继续跑”，不是“重启流程”。

第一版唤醒后：

- 继续回到 `loop()`
- 重新执行 `g_app.update(now_ms)`
- 再决定是否需要 `render()`

不要求保存复杂恢复上下文。

## 4. 状态机设计

建议新增一个轻量运行态，而不是直接在 `loop()` 里裸调 `esp_light_sleep_start()`。

建议新增：

- `Awake`
- `SleepEligible`
- `Sleeping`

第一版不需要把它做成非常重的公共模块，但需要明确行为。

### 4.1 `Awake`

含义：

- 当前正在处理输入、WiFi、渲染、模式切换或其它活跃逻辑

进入条件：

- 开机默认进入
- 任何输入事件到来
- 任何页面切换
- 任何渲染请求产生
- 任何 WiFi 会话启动

### 4.2 `SleepEligible`

含义：

- 当前系统没有活跃工作，可以开始计算“下一次最早需要醒来的时刻”

进入条件：

- `update()` / `render()` 一轮结束后，满足所有“可进入 light sleep”的硬条件

### 4.3 `Sleeping`

含义：

- 已配置唤醒源并执行 `esp_light_sleep_start()`

退出条件：

- 按键唤醒
- RTC 定时器唤醒

## 5. 唤醒源设计

### 5.1 按键唤醒

当前按键：

- `GPIO0`：Up
- `GPIO35`：Mid
- `GPIO34`：Down

第一版调整为：

- `Up / Mid / Down` 全部作为 `light sleep` 唤醒源

设计说明：

- `GPIO0` 仍然是启动绑定位，风险点主要发生在上电/复位采样窗口
- 当前项目接受这部分低频启动级风险，以换取三键交互一致性
- 因此实现时不再把 `GPIO0` 排除在唤醒源之外

额外验收要求：

- 验证 `light sleep` 下三键都能正常唤醒
- 验证按住 `Up` 手动复位时的启动行为
- 验证烧录后自动重启时的启动行为

### 5.2 RTC 定时器唤醒

RTC 唤醒时间取决于当前页面：

- `Photo`
  - 下一次轮播时间：`last_photo_switch_ms_ + photo_interval_ms_`
- `Calendar`
  - 下一次时间头局刷时间桶边界
  - 或下一次周期全刷时间点

最终取“最早需要处理的那个时间点”。

## 6. 各模式策略

### 6.1 `Normal + Photo`

最适合进入 `light sleep`。

睡眠时机：

- 当前不需要渲染
- 当前没有配置模式
- 当前没有 WiFi 会话

RTC 目标：

- 直到下一次自动轮播时间

按键唤醒后行为：

- 继续当前页面
- 用户短按触发切图或切页

### 6.2 `Normal + Calendar`

同样适合进入 `light sleep`，但 RTC 计算更复杂。

睡眠时机：

- 当前没有待渲染
- 不在全刷前同步等待中
- 当前没有 WiFi 会话

RTC 目标：

- 下一次 `calendar_time_refresh_sec` 时间桶边界
- 或下一次 `calendar_refresh_sec` 周期全刷检查点

取更早者。

注意：

- 如果当前 `force_calendar_full_refresh_ == true`，不进入 sleep，先完成渲染链
- 如果正在等待 STA/日历同步，绝不进入 sleep

### 6.3 `ConfigWait`

第一版不进入 `light sleep`。

原因：

- 停留时间只有 60 秒
- 用户可能在等按键选择 AP / STA / 白屏
- 第一版先保证行为稳定，不引入中途睡眠干扰

### 6.4 `ConfigAP / ConfigSTA`

第一版不进入 `light sleep`。

原因：

- AP/STA 会话期间有 HTTP 请求、WiFi 协议栈和同步行为
- 当前 `WifiManager` 还不是 sleep-aware

## 7. 需要新增的接口

### 7.1 `App`

建议新增：

- `bool canEnterLightSleep(uint32_t now_ms) const;`
- `uint32_t nextWakeDeadlineMs(uint32_t now_ms) const;`
- `void onWakeFromLightSleep(uint64_t slept_us);`

职责：

- 判断当前是否允许睡眠
- 提供最早唤醒截止时间
- 在唤醒后做必要的轻量日志和状态修正

### 7.2 `WifiManager`

建议新增：

- `bool blocksLightSleep() const;`

第一版返回 `true` 的条件：

- `state_ != Idle`
- 或 `calendar_sync_pending_`
- 或 `last_calendar_sync_status_ == "running"`

作用：

- 把 sleep 判定从 `App` 里隔离出来

### 7.3 `InputManager`

第一版可以不改接口。

原因：

- 唤醒后仍会进入正常 `update()`，现有轮询和事件队列足够承接

## 8. 时间计算设计

建议统一通过“绝对 deadline”计算睡眠时长。

### 8.1 Photo deadline

```text
photo_deadline = last_photo_switch_ms_ + photo_interval_ms_
```

### 8.2 Calendar deadline

由两部分取最小值：

- 时间头局刷 deadline
- 周期全刷检查 deadline

时间头局刷 deadline：

- 根据 `calendar_time_refresh_sec`
- 计算下一个 bucket 边界时刻

周期全刷 deadline：

- 根据 `last_calendar_check_ms_` 和 `calendar_refresh_sec`

### 8.3 安全边界

建议增加两个保护：

- 最小睡眠时长：例如 `200ms`
- 最大单次睡眠时长：例如 `5min`

原因：

- 太短的睡眠没有意义
- 太长不利于第一版调试

## 9. 主循环改造方案

当前 `loop()`：

```cpp
void loop() {
  const uint32_t now_ms = millis();
  g_app.update(now_ms);
  g_app.render();
  delay(50);
}
```

建议改成：

1. 先 `update()`
2. 再 `render()`
3. 如果 `App` 判断可睡：
   - 配置按键唤醒源
   - 配置 RTC 唤醒时间
   - 执行 `esp_light_sleep_start()`
   - 唤醒后通知 `App`
4. 如果不可睡：
   - 回退到短 `delay`

这样可以保证：

- 现有行为兼容
- `light sleep` 只是替换了“空转等待”这段时间

## 10. 风险点

### 10.1 `GPIO0`

它既是按键，又涉及启动模式。

当前处理策略：

- 第一版仍把 `GPIO0` 纳入唤醒源
- 但把相关启动级风险纳入真机验收，而不是在设计上回避

### 10.2 `millis()` 跳变

`light sleep` 唤醒后，`millis()` 会继续增长。

当前代码大量用 `millis()` 做 deadline/interval 计算，这对 `light sleep` 是友好的。
但仍需验证：

- `last_photo_switch_ms_`
- `last_calendar_check_ms_`
- `sta_connect_start_ms_`

在唤醒后不会因为大步跳变触发意外超时。

### 10.3 WiFi 会话阻塞

如果错误地在 WiFi 会话期间进入 `light sleep`，可能导致：

- AP/STA 异常
- WebServer 请求中断
- 日历同步失败

所以第一版必须简单保守：只要 `WifiManager` 不空闲，就不睡。

### 10.4 显示会话期间睡眠

显示会话中途绝不能进入 `light sleep`。

第一版保证：

- 只有 `render()` 完成且 `needs_render_ == false` 才允许睡

## 11. 第一版实现顺序

### 第一步

新增一个小模块，例如：

- `src/system/LightSleepController.*`

职责：

- 配置按键 + RTC 唤醒源
- 执行 `esp_light_sleep_start()`
- 返回唤醒原因和睡眠时长

### 第二步

给 `App` 增加：

- `canEnterLightSleep()`
- `nextWakeDeadlineMs()`
- `onWakeFromLightSleep()`

### 第三步

给 `WifiManager` 增加：

- `blocksLightSleep()`

### 第四步

改 `main.cpp` 的 `loop()`

实现：

- 默认走 `light sleep`
- 不满足条件时才回退 `delay(50)`

### 第五步

第一轮只开放：

- `Normal + Photo`
- `Normal + Calendar`

其它模式保持禁用。

## 12. 第一版验收

逻辑验收：

- `Photo` 静置时能进入 `light sleep`
- 到轮播时间点能自动唤醒并换图
- `Calendar` 静置时能进入 `light sleep`
- 到下一个时间桶时能自动唤醒并触发 header time 刷新
- `Up / Mid / Down` 都能把系统从 `light sleep` 唤醒

边界验收：

- `ConfigWait / ConfigAP / ConfigSTA` 不进入 `light sleep`
- 全刷前同步等待期间不进入 `light sleep`
- WiFi Portal 访问不中断

功耗验收：

- 对比当前固定轮询版，`Normal + Photo` 待机电流明显下降
- `Normal + Calendar` 待机电流明显下降

## 13. 当前推荐结论

对当前代码，最稳的 `light sleep` 路线是：

1. 只在 `Normal` 模式下启用
2. 只在 `WifiManager` 完全空闲时启用
3. 使用 `Up / Mid / Down + RTC` 作为第一版唤醒源
4. 先跑通 `Photo` 和 `Calendar` 的等待期睡眠
5. 稳定后再评估是否扩大到更多模式或升级到 `deep sleep`
