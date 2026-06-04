# Phase 12 Face System Spec

## 目标

Face System 保留 mumuer1024 二次开发版的 7 个静态命令和 10 个动画命令，但不复制其自动生成的 6x6 `fillRect` 图案。M5Dial 版面向 240x240 圆屏重新设计：大轮廓、少文字、强特征、矢量/块混合绘制，全部运行在 4bpp 调色板 `M5Canvas` 上。

约束：
- 不使用位图文件，不依赖 LittleFS 资源。
- 所有有意义元素落在中心 `(120,120)`、半径 `110` 的安全圈内。
- 动画必须由 `tick()` 非阻塞推进，不允许 `delay()`。
- Web 可见 API 使用稳定 key，如 `face_wuyu` / `anim_smile`，不暴露 `Face 0..16`。

## 注册表

`src/faces/faces_data.{h,cpp}` 提供 `FaceSpec`：

| 字段 | 含义 |
|---|---|
| `key` | WS 命令 key |
| `label_en` / `label_zh` | Web 显示名 |
| `animated` | 是否动画 |
| `frame_count` | 1 到 3 帧 |
| `loop_count` | 静态 1；循环动画 3；单次动画 1 |
| `hold_final` | 动画完成后是否停最后帧 |
| `frame_ms` | 非阻塞帧间隔 |

## 静态表情草图

| key | 草图 | 设计 |
|---|---|---|
| `face_wuyu` | `-_-` | 半闭长眼 + 平嘴，整体稍低，表达无语。 |
| `face_wenhao` | `?` | 顶部块状问号 + 小疑惑眼，瞳孔上看。 |
| `face_gantanhao` | `!` | 两侧瞪眼 + 中央琥珀感叹号。 |
| `face_angry` | `><` | 内斜眉 + 斜向眯眼 + 锯齿嘴。 |
| `face_yes` | check | 弯眼微笑 + 大号绿色对号。 |
| `face_X` | `xx` | 双 X 眼 + 小平嘴，红色强调。 |
| `face_glass` | sunglasses | 连续墨镜带 + 白色高光 + 小酷嘴。 |

## 动画表情草图

| key | 帧 | 播放 |
|---|---|---|
| `anim_jiyanjing` | 开眼 -> 单眼闭合 -> 回弹睁大 | 3 帧，3 遍，结束回第 0 帧。 |
| `anim_yun` | 双螺旋眼 3 个旋转相位 | 3 帧，3 遍，结束回第 0 帧。 |
| `anim_close` | 开 -> 半闭 -> 闭 | 3 帧，3 遍，结束回第 0 帧。 |
| `anim_dead` | 怔住 -> X 眼 -> 下沉灰化 | 3 帧，3 遍，停最后帧。 |
| `anim_dian` | 一点 -> 两点 -> 三点 | 单次，停最后帧。 |
| `anim_smile` | 普通笑 -> 大笑脸颊 | 2 帧，3 遍，结束回第 0 帧。 |
| `anim_look` | 瞳孔左 -> 中 -> 右 | 3 帧，3 遍，结束回第 0 帧。 |
| `anim_hart` | 小心 -> 大心 | 2 帧，3 遍，结束回第 0 帧。 |
| `anim_zzz` | 闭眼 -> 小 z -> 大 z | 单次，停最后帧。 |
| `anim_ganga` | 斜眼 -> 汗滴出现 -> 汗滴下滑 | 单次，停最后帧。 |

## 渲染实现

- `FaceShow` 在 `onEnter()` 创建 240x240 4bpp sprite，约 28KB SRAM。
- 调色板索引固定：`kBg/kInk/kCream/kWhite/kAmber/kAmberHi/kGreen/kRed/kCyan/kGray/kRose`。
- `FaceShow::tick()` 根据 `FaceSpec` 计算当前帧，只在帧变化时 `redraw(frame)`。
- `applyState()` 收到新 `face_index` 时重启动画；背景色变化只更新 `kBg` 调色板并重绘当前帧。

## Web / WS

- Client -> Device：`{ "type": "set_face", "key": "face_wuyu" }`
- Device -> Client state：`face_key`
- Web 下拉显示真实表情名，value 使用稳定 key。

## 验证

1. `pio run` 通过。
2. Faces 下拉可选择全部 17 个 key。
3. 静态脸切换后立即显示；动画脸按帧播放，不阻塞其他 WS 命令。
4. 动画播放中切 mode 或切另一个 face 不崩溃、不残影。
5. 先进 Canvas，再进 Claude Link，再进 Faces，串口 `freeheap` 仍有余量。
