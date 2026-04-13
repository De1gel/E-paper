# WiFi Portal Test Plan（按当前代码）

更新时间：2026-04-10

## 1) 配置模式入口 / 退出

- 长按中键约 `600ms` -> 进入 `ConfigWait`
- `ConfigWait` 下 LED 快闪
- `60s` 无按键操作 -> 自动回到 `Normal`
- `ConfigWait` 中短按中键 -> 白屏动作 -> 返回 `Normal`

## 2) AP 模式

- 在 `ConfigWait` 中短按上键 -> 进入 AP
- 连接 `PhotoFrame_Config / 12345678`
- 打开 `http://192.168.4.1/`

检查项：
- SPIFFS 已上传时，能看到完整 Portal 页面
- SPIFFS 未上传时，能看到最小 fallback 提示页
- `GET /api/status` 正常
- `GET /api/settings` / `POST /api/settings` 正常
- `GET /api/calendar/events` / `POST` / `DELETE` 正常
- `GET /api/files`、`POST /api/dir`、`GET /api/file`、`DELETE /api/file` 正常
- `POST /api/upload` 正常
- `POST /api/stop` 能退出 Portal
- `POST /api/reboot` 能触发重启

超时检查：
- AP 空闲超时当前为 `300s`
- 若仍有终端连接 AP，活动时间会持续刷新

## 3) STA 模式

- 先在设置页写入真实 `sta_ssid / sta_pass`
- 在 `ConfigWait` 中短按下键 -> 进入 STA 连接
- 串口应输出 LAN IP
- 打开 `http://<device_ip>/`

检查项：
- `GET /api/geocode?city=...` 正常
- `GET /api/weather_test` 仅在 STA 已连通时成功
- 校时成功后，串口能看到时区 / 本地时间日志

超时检查：
- STA 连接超时当前为 `30s`
- STA 运行态空闲超时当前关闭，不应再按旧文档的 `120s` 预期测试

## 4) 日程与文件

### 日程

- 添加 `weekly` / `daily` / `once` 三类日程
- 删除单条日程
- 重启设备后确认日程仍能读回

### 文件

- 新建目录
- 上传一个普通文件
- 上传一张图片并走 `fit` 或 `crop` 浏览器预处理路径
- 下载文件
- 删除文件
- 删除目录

## 5) 路径安全

- 调用 `/api/files?path=../` -> 返回 `bad_path`
- 调用 `/api/file?path=../x` -> 返回 `bad_path`
- 对根目录 `/` 做删除 -> 应被拒绝

## 6) 回归项

- 照片轮播仍按 `photo_interval_sec` 工作
- 中键短按仍能切换 Photo / Calendar
- 白屏动作仍可执行
- 日历页仍能读取手动日程并渲染
