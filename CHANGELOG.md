# Changelog

本项目所有重要变更记录于此。
格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循 [Semantic Versioning](https://semver.org/lang/zh-CN/)。

## [Unreleased]

## [0.3.0] - 2026-06-06

v0.3.0（M5Dial 硬件交互 + 模式整合，Phase 17-19；详见 `git log v0.2.0..v0.3.0` 与 `docs/v030_spec.md`）。

### Added

- **旋转编码器本地操作**：单击旋钮在 5 mode 间轮询（Faces → Claude Code → Canvas → Claude Link → PC Monitor）；旋转 = 上下文拨盘（Faces 浏览 18 表情 / PC Monitor 切分类卡 / Claude Link 切被监视项目 / Canvas 转半圈清屏）
- **触摸手势**：Tap = poke Clawd（Faces 眨眼/wink 反应）；长按 = 锁定/解锁自动行为（Faces 轮换 & PC Monitor 切卡）；Canvas 触摸 = 本地画线（圆形 mask 内，原始按压态）
- **Canvas 设备端清屏**：旋钮转半圈（8 detent）填满最外圈橙红进度弧 → 清屏；停转 0.8s 自动归零、双向旋转均累积（进度弧可见 + 半圈阈值避免误触）；首次进入改为空白画布（移除开发期测试图案）
- **蜂鸣器音效**（`M5Dial.Speaker`）：编码器 detent 咔 / 切 mode 80ms / Canvas 落笔 20ms / WS 连接上扬双音
- **Faces 自动轮换**：动画播一次→静置 10s→随机换一张（可重复并重播；旋钮浏览 / 长按锁定可中止）
- **Claude 风味彩蛋**：开机唤醒鸣音；Claude Link 声音通知（会话转 waiting 注意音 / 转 idle 完成音，环境通知器）；切 mode 时 Claude 星芒径向擦除转场；Faces 静置 3min 打盹（zzz 脸 + 调暗，任意输入唤醒）
- 招牌摇摆眼表情 `anim_idle`（移植原 Normal Eyes，作开机/home 脸；Faces 17 → 18）

### Changed

- **模式整合**：Normal Eyes / Squish Eyes 并入统一 Faces mode（本质同为"画表情"）；开机默认进 Faces（home = `anim_idle` 摇摆眼）；`ModeId` 重编号为 6 槽（`FACE_SHOW`=0 .. `REMINDER_OVERLAY`=5）
- Web 面板移除两个 eyes mode chip（nav 按设备轮询序重排）；Speed 控件迁入 Claude Link 面板（claude_status idle 节奏是其唯一消费者）
- **CLAUDE.md §9 更正**：蜂鸣器改用 `M5Dial.Speaker.tone()`，不再裸 `ledcWriteTone`（会与 M5Unified Speaker 驱动争用 G3）

### Removed

- `src/modes/eyes_normal.{h,cpp}` / `src/modes/eyes_squish.{h,cpp}`（功能并入 Faces mode）

## [0.2.0] - 2026-06-04

v0.2.0 移植 mumuer1024 二次开发功能 + 本项目原创 Claude Code 联动（Phase 9-15，详见 `git log v0.1.0..v0.2.0` 与 `docs/phase*_spec.md`）。

### Added

- **WiFi 配网 + STA 运行**：按需开放配网 AP `Clawd-Mochi-Setup`（captive portal + 屏幕二维码）→ 存 NVS → 重启进 STA，经 mDNS `clawd-mochi.local` 访问，取代 v0.1.0 常驻控制 AP
- **Settings 页**：Web 改 PC IP / Reset WiFi / 重启
- **Claude Code 联动状态模式（本项目原创招牌特性）**：设备表情实时反映会话态（思考中 / 待确认 / 待命），提供 CLAWD 情绪眼 / SPARKLE 星芒核心两种可选风格（Settings 切换 + 记忆），由 PC 端 cc_hooks（纯 curl）推 `GET /cc` 驱动；非 canvas 模式对话自动切入，受 [DemoJj/claude-code-traffic-light](https://github.com/DemoJj/claude-code-traffic-light) 启发
- **Face System**：17 种圆屏友好表情（不照搬原 6×6 像素码，重设计为黑硬边方眼语言），Web 下拉按 key 切换
- **Reminder 定时提醒**：≤5 条每日 HH:MM 提醒（NTP 取时），到点全屏弹出 30s 后自动回原 mode
- **PC Monitor 模式 + Python 服务**：6 大分类圆形面板（CPU / 磁盘 / 显卡 / 主机 / 网络 / 流量，弧形仪表 + 进度条 + 每类强调色），默认 20s 轮询切换；配套服务 psutil 跨平台基础数据 + LibreHardwareMonitor（Windows 可选）补温度 / 风扇 / GPU / 功耗

### Changed

- **默认开机进设备界面**（单机可玩），配网改为按需进入（BtnA 长按 5s / Settings Reset WiFi）；不再首启强制配网
- **网络模型反转**：v0.1.0 常驻 AP → v0.2.0 正常 STA-only / 配网 AP_STA，二者经重启切换、永不在有动画时共存
- **设备身份底色：红 → 橙红 `#ff4000`**（影响所有 mode）
- **Web 控制面板重构**：扁平长列表（不相关控件弱隐藏仍占位）→ mode 入口 chip 网格 + 每 mode 内联 `.panel` 容器 + 全局常驻区；PC Monitor 容器新增网页实时镜像（页面直接 fetch PC 服务，零固件改）
- **README 双语补 v0.2.0 章节**：修正过时的常驻 AP / 控制信息，补全功能列表与首次配网说明

### Fixed

- 联动状态模式黑边闪烁（改 4bpp 调色板离屏缓冲，每帧一次 `pushSprite`，本机无 PSRAM 故用调色板省内存与 Canvas sprite 共存）
- SPARKLE waiting `!` 心跳期半刷新闪烁
- cc_hooks hook 命令退出码缺陷（curl 失败冒泡成 hook 报错，加 `|| true` / `exit 0` 兜底）

## [0.1.0] - 2026-05-28

v0.1.0 完成原项目 4 mode 移植 + Web 控制（Phase 0-7，详见 `git log v0.1.0`）。

### Added

- 4 mode 核心移植：EyesNormal / EyesSquish / ClaudeCode / Canvas
- 圆屏 UI 适配：GC9A01 240×240、安全区 r ≤ 110、端点级圆形 mask
- WiFi AP（`ClaWD-Mochi` / `clawd1234`，192.168.4.1）+ AsyncWebServer + WebSocket 控制面板（mode / speed / bg / pen / backlight / clear / terminal / stroke 共 8 个 message type）
- Web 端实时笔触同步（HTML5 canvas drawing pad，pointer events + 50ms 节流）
- SharedState 全局协议真值 + 多 client 状态广播
- pc_monitor/ Python 服务整套移植（v0.2.0 联调备用，本版未启用）

### Changed

- 基于 PlatformIO + Arduino + M5Dial 库**从零重写**，与原 ESP32-C3 + Adafruit GFX 代码无任何复用关系
- `platformio.ini` 加 `-DARDUINO_USB_CDC_ON_BOOT=1`、`lib_deps` 引入 ESP32Async fork（`ESPAsyncWebServer @ ^3.11.0` + `AsyncTCP @ ^3.4.10` + `ArduinoJson @ ^7.0.0`）

### Fixed

- 无
