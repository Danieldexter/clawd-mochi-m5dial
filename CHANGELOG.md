# Changelog

本项目所有重要变更记录于此。
格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循 [Semantic Versioning](https://semver.org/lang/zh-CN/)。

## [Unreleased]

（v0.2.0 累积区）

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
