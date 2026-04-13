# 图片上传与颜色处理笔记

更新时间：2026-04-10

## 1. 当前照片主链路

当前照片显示已经不走 `src/image.cpp` 数组资源路径，而是：
- SD 卡目录：`/pic`
- 文件格式：`.epd4`
- 写屏方式：流式写入面板 RAM

当前照片页只会把以下文件计入轮播：
- 扩展名为 `.epd4`
- 文件大小等于 `800 * 480 / 2 = 192000 bytes`

## 2. 当前上传处理链

### 浏览器侧

`data/app.js` 已实现图片预处理：
- `normal`：原样上传
- `fit`：按比例缩放到 `800x480`
- `crop`：优先铺满 `800x480`，允许裁边

前端当前还支持：
- 竖图先旋转到横向
- 抖动算法：
  - `fs_serpentine`
  - `atkinson`
  - `jjn`
- `gamma` 调整：`0.80 ~ 1.40`

最终输出：
- 浏览器端生成 `.epd4`
- 再通过普通上传接口写入 SD

### 固件侧

固件当前不做服务端图片预处理：
- `mode=normal`：原样保存
- `mode=fit` / `mode=crop`：直接拒绝
- 错误码：`server_preprocess_disabled_use_browser`

## 3. 当前 `.epd4` 约束

- 分辨率：`800x480`
- packed 格式：`4bit / pixel`
- 每字节两个像素
- 文件大小固定：`192000 bytes`

这也是 `App` 在扫描 `/pic` 时用来判断有效照片文件的条件。

## 4. `ColorMap` 模块的当前位置

当前仓库仍保留：
- `src/display/ColorMap.h`
- `src/display/ColorMap.cpp`

它的作用是：
- 面板颜色映射
- Bayer 4x4 混色辅助
- 离线实验与校色参考

但要明确：
- 它不是当前 Portal 上传主链路的一部分
- 当前 Portal 的图片量化主要在浏览器端完成

## 5. 旧路径与资料参考

### 旧路径

- `src/image.cpp`
- `include/image.h`

说明：
- 这是早期图片数组路径
- 当前主构建已经排除 `src/image.cpp`

### 参考资料

- `设计思路资料/Image2LCD软件图片取模说明.pdf`

这个资料现在更适合：
- 离线批量制图参考
- 理解老的取模思路

而不是当前 Portal 主流程的第一入口。

## 6. 实际使用建议

- 优先使用 Portal 的 `fit` / `crop` 上传
- 照片最终应落到 `/pic/*.epd4`
- 若 Portal 页面更新过，记得同步 `uploadfs`
- 如果需要复现实验结果，记录抖动算法与 gamma 参数
