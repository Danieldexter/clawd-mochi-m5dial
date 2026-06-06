# v0.3.0 Spec — M5Dial 硬件交互 + 模式整合

> Phase 17（编码器）/ 18（触摸）/ 19（蜂鸣器）+ 模式整合 + 4 个 Claude 风味彩蛋。
> 设计依据：已批准计划 `~/.claude/plans/keen-herding-goose.md`。本文件是交互真值表 + earcon 表 + HW 标定清单。

## 1. 输入模型（无 Event 抽象，loop 内直接派发）

`IMode` 仍只有 `onEnter/onExit/tick/applyState`（§1.2 不做投机抽象）。本地输入在 `main.cpp` `loop()` 派发：
薄设备封装 `input/encoder`（`poll()` → detent 增量）、`input/touch`（`poll()` → `Gesture`），main.cpp dispatch helper
（`handleButton/handleEncoder/handleTouch/handleFaceAutoCycle/handleIdleDoze`）决定各 mode 行为，统一改 `SharedState` / `setMode`。

BtnA（旋钮机械下压）与 Touch（玻璃电容）是两路独立输入；防误触：本帧 BtnA 已 click 则跳过 touch Tap。
输入派发与 Reminder overlay **互斥**（overlay 期间只处理 dismiss，不轮询/拨盘）。

## 2. 交互真值表（输入 × mode → 动作）

| 输入 | Faces | Claude Code | Canvas | Claude Link | PC Monitor |
|------|-------|-------------|--------|-------------|------------|
| **旋钮单击** | → 下一 mode（轮询）+ 80ms 音 + 星芒转场 | 同左 | 同左 | 同左 | 同左（回 Faces）|
| **旋钮旋转** | 浏览 18 表情（环绕）+ 每 detent 咔 + 重置静置 | — | — | 切 cc_scope（[全部]+已注册项目）| 切分类卡（暂停自动轮询）|
| **Tap** | poke → wink 反应 ~1s 回原脸 | — | —（绘画走按压态）| — | — |
| **长按（触摸）** | 锁定/解锁自动轮换 | 同（全局，无副作用）| 同 | 同 | 锁定→停自动切卡（编码器仍可切）|
| **按压拖动**（Canvas）| — | — | 本地画线（圆 mask，原始按压态 `isPressed()`）+ 落笔音 | — | — |
| **BtnA 长按 5s** | 进 WiFi 配网（全局，既有）| — | — | — | — |

非交互自动行为（仅 Faces）：动画播一次后静置 `kFaceRestMs`(10s) 随机换脸（完全随机、可重复并重播；单张停留=动画时长+静置；锁定/doze 时停）；静置 `kIdleDozeMs`(3min) → 打盹（zzz + 调暗 `kDozeBrightness`），任意输入唤醒（恢复亮度 + 哈欠音 + 回 idle 脸，吞掉该次输入）。

## 3. 模式整合（Eyes → Faces）

- 删 `eyes_normal`/`eyes_squish`；`ModeId` 6 槽：`FACE_SHOW`=0 / `CLAUDE_CODE`=1 / `CANVAS`=2 / `CLAUDE_STATUS`=3 / `PC_MONITOR`=4 / `REMINDER_OVERLAY`=5（0..4 入轮询）。
- Normal Eyes 摇摆+眨眼移植为 `anim_idle`（faces 索引 17 = `kFaceIdle`，`kFaceCount`=18），FaceShow 检测 idle 走 `faces::drawIdleEyes` 连续渲染（非离散帧），开机 home。Squish 笑 = 既有 `anim_smile`。
- `speed` 字段保留（claude_status idle 节奏唯一消费者）；Web Speed 控件迁入 Claude Link 面板。

## 4. Earcon 表（`src/audio/beeper.cpp`，`M5Dial.Speaker.tone`）

| 函数 | 触发 | 大致频率/时长（HW 可调）|
|------|------|------|
| `tick()` | 编码器每 detent | 2600Hz / 8ms |
| `modeSwitch()` | 旋钮单击切 mode | 1760Hz / 80ms |
| `penDown()` | Canvas 落笔（拖动起始去重）| 2200Hz / 20ms |
| `clientConnect()` | WS 新客户端 | 988→1480Hz 上扬 |
| `pinToggle(on/off)` | 长按锁定/解锁 | 1200↑1600 / 1600↓1200 |
| `bootChime()` | 开机 | 880→1320Hz 上扬 |
| `attention()` | Claude Link → waiting | 1397→1864Hz |
| `success()` | Claude Link → idle（自 working/waiting）| 1318→1976Hz |
| `yawn()` | 打盹唤醒 | 1100↓660Hz 下行 |

音量 `kVolume=90`（0..255）。双音用第二 tone `stop_current_sound=false` 排队（Speaker 每通道双缓冲）。

## 5. 星芒转场

`src/ui/transition.cpp` `playSparkleWipe(bg)`：由圆心扩张 bg 圆盘（径向擦旧）+ 前缘 8 向星点，半径到 176 覆盖四角，≤150ms 一次性同步（类比 reminder/QR 屏既有同步绘制）。切 mode 时在 `setMode` 前调（旧屏 → 擦除 → 新 mode `onEnter` 在干净 bg 重绘）。

## 6. config 常量（`config.h`）

`kEncoderCountsPerDetent=4`（HW 标定，部分单元 2）/ `kFaceRestMs=10000` / `kIdleDozeMs=180000` / `kDozeBrightness=20`。

## 7. HW 标定清单（实测调）

1. **编码器 detent 折算**：一格物理 detent 应 = 一步（÷4 vs ÷2）；不对则改 `kEncoderCountsPerDetent`。方向：顺时针应 +（face_index++/下一卡）。
2. **Speaker**：确认有声（`M5Dial.begin` 应已启用内部 Speaker）；`kVolume` 过响下调；各 earcon 频率悦耳度。
3. **触摸**：Tap/长按用 M5 默认手势；确认 Faces tap 不误触发 mode 切换（BtnA 优先守卫生效）；Canvas 用**原始按压态 `isPressed()`** 绘画（不靠 isDragging，避开 8px 阈值与慢按 hold 误判），连续描画不断线、细/慢笔也落点。
4. **星芒转场**：≤150ms 不显卡顿；四角擦净；切到 pc_monitor/claude_status（自有底色）短暂橙闪可接受。
5. **打盹**：3min 无输入进 zzz + 调暗；任意输入唤醒 + 哈欠；非 Faces mode 不打盹。
6. **离屏 sprite 预算（v0.3.0 实测修）**：face_show/claude_status/pc_monitor 在 `onExit` 释放各自 4bpp 28KB（只保活动 mode）；Canvas 16bpp 115KB 长驻留存画作。峰值 115+28=143KB（已证可容）。轮询经 Canvas 后切 Claude Link 应正常显示（修前 OOM → `createSprite` 失败 → 黑屏）；反复轮询全 5 mode 看 `freeheap` 稳定不漏。
7. **回归**：BtnA 长按 5s 仍进配网；Web eyes chip 消失、Faces 统一、Speed 在 Claude Link、current_mode/face 回填。

## 8. 编译基线

`pio run` 通过：RAM 16.7%(54868B) / Flash 51.2%(1712781B)，较 v0.2.0 基线 Flash +~17.5KB（input/audio/ui + idle eyes + 彩蛋 + 实测 4 修）、RAM +24B。
