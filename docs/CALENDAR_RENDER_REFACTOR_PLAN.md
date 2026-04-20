# 无 PSRAM 日历渲染现状与后续重构

更新时间：2026-04-10

## 1. 当前判断

当前代码已经从“日历逻辑全部堆在 `App.cpp` 里”的阶段往前走了一大步，但还没有彻底完成无 framebuffer 主路径化。

### 已落地

- `CalendarModel`
- `CalendarLayout`
- `CalendarScene`
- `CalendarText`
- `StripeBuffer`
- `SceneRasterizer`

### 仍未落地

- `CalendarStore`
- `CalendarSyncService`
- `RefreshPlanner`
- `EpdSession`

## 2. 当前代码中的日历主链路

当前入口仍在：
- `src/app/App.cpp`

当前渲染步骤：
1. `renderCalendarPage()` 获取当前本地时间
2. `buildCalendarModel()` 生成日历数据
3. `buildCalendarLayout()` 生成页面布局
4. 全刷主路径调用 `pushCalendarFullRefreshStriped()`
5. 局刷兼容路径调用 `drawCalendarScene()` 写入 `calendar_frame_`
6. 再由 `pushCalendarPartialRefresh()` 把 packed buffer 写入面板

## 3. 目前已经完成的重构结果

### 3.1 数据 / 布局 / 场景已拆开

现在 `App` 不再直接负责所有日历内容组织：
- 数据放在 `CalendarModel`
- 几何布局放在 `CalendarLayout`
- 图元输出放在 `CalendarScene`

### 3.2 条带全刷已进入主路径

当前日历页全刷已经不再依赖整屏 framebuffer 输出：
- `StripeBuffer` 管理条带缓冲
- `SceneRasterizer` 把场景按条带栅格化
- `pushCalendarFullRefreshStriped()` 逐条带写屏

这意味着：
- “条带渲染 + 全刷稳定”已经不是计划，而是当前实际代码路径

### 3.3 本地事件已接入 model

当前手动日程来自 `WifiManager` 持久化的数据：
- Portal 可新增 / 删除 / 修改日程
- `CalendarModel` 会读取这些事件并整理成当日可见列表

## 4. 仍然存在的结构问题

### 4.1 `calendar_frame_` 仍然存在

当前 `App::begin()` 里仍会尝试：
- `ensureCalendarFrameBuffer("boot")`

这说明：
- 整屏 framebuffer 还没有完全退出默认运行路径
- 当前代码仍要承担这块内存分配风险

### 4.2 局刷路径仍依赖整屏 packed frame

当前 partial 路径的基本模式仍是：
- 先把整页内容画到 `calendar_frame_`
- 再调用 `writeWindowFromPacked(...)`

这和目标架构仍有差距：
- 还没有真正基于脏区只重绘相关条带
- 当前 partial 仍属于兼容 / 实验态

### 4.3 日历数据仍挂在 `WifiManager`

当前 `WifiManager` 仍同时承担：
- 网络
- Portal
- 设置持久化
- 手动日程存储

这会让后续远端同步继续和 WiFi 层耦合。

## 5. 当前固定决策

- 无 PSRAM 仍然是默认硬件前提
- 日历页主路径以“条带全刷稳定”为先
- 局刷保留，但不是当前产品交付前提
- 照片页继续保持 SD 流式写屏方案
- 远端日历同步暂未接入

## 6. 下一阶段建议

### 6.1 优先收掉 `calendar_frame_` 的默认依赖

建议先做：
- 不在 `boot` 时预分配整屏 framebuffer
- 把 partial 路径改为显式实验开关或按需分配

### 6.2 把日历数据源从 `WifiManager` 抽离

建议新增：
- `CalendarStore`
- `CalendarSyncService`

目标：
- 手动事件与未来同步事件走同一数据入口

### 6.3 引入刷新规划器

建议新增：
- `RefreshPlanner`

目标：
- 把“什么时候全刷 / 局刷”从 `App` 中拿出来
- 为后续 dirty rect 机制预留清晰边界

### 6.4 最后再决定局刷是否进入主路径

只有满足以下条件后才考虑纳入：
- 条带主路径稳定
- 脏区合并逻辑正确
- 局刷残影与闪烁可接受
- 不破坏跨天、布局切换和页面切换的稳定性
