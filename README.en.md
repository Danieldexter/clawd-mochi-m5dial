# clawd-mochi-m5dial

> M5Dial remix of [clawd-mochi](https://github.com/yousifamanuel/clawd-mochi).
> Desktop companion device: round display + rotary encoder + touch + Wi-Fi control.

中文：[README.md](./README.md)

## Hardware

- [M5Stack M5Dial](https://docs.m5stack.com/en/core/M5Dial) (ESP32-S3 + 1.28" round touch display + encoder + buzzer)
- USB-C cable

## Features

- **Animated eyes**: normal (sway + blink) and squish (`> <`)
- **Claude Code terminal**: scrolling terminal-text animation
- **Canvas**: draw from the web UI, mirrored live on the device
- **Face System**: 17 round-display faces, switchable from a web dropdown
- **Claude Code status link** (original to this project): with hooks configured, the device's expression reflects your session state in real time — working / waiting / idle
- **PC Monitor**: shows your computer's CPU / memory / GPU / network / disk and more (requires the companion service running on the PC)
- **Reminders**: up to 5 daily reminders, full-screen pop-up when due, auto-returns after 30 s

### Demo

Claude Code status link — the device reflects your session state live; view and switch styles from the web:

<p align="center">
  <img src="docs/images/claude%20Link%20%E8%AE%BE%E5%A4%87%E6%BC%94%E7%A4%BA.jpg" width="320" alt="Device Claude status link demo">
  <img src="docs/images/WEB%20claude%20Link.png" width="320" alt="Web Claude Link controls">
</p>

PC Monitor — a round panel on the device, mirrored live in the browser:

<p align="center">
  <img src="docs/images/pc%20monitor%20%E8%AE%BE%E5%A4%87%E6%BC%94%E7%A4%BA.jpg" width="320" alt="Device PC Monitor demo">
  <img src="docs/images/WEB%20PC%20Monitor.png" width="320" alt="Web PC Monitor mirror">
</p>

## Build & flash

Requires [PlatformIO Core](https://platformio.org/install/cli) or VSCode + PlatformIO IDE extension.

```pwsh
pio run                      # compile
pio run -t upload            # flash firmware
pio run -t uploadfs          # upload web assets to LittleFS (required after editing data/)
pio device monitor -b 115200 # serial monitor
```

## First-time setup

1. On power-up the device goes straight to its display — fully usable offline, no network required.
2. For web control: **long-press the screen button for 5 seconds** to enter provisioning. Join the open hotspot `Clawd-Mochi-Setup`, the setup page pops up automatically (or scan the QR code on screen), enter your Wi-Fi and PC IP, and the device reboots onto your network.
3. Then open **`http://clawd-mochi.local`** from any browser on the same LAN to control it.

## Controls

- **Web panel** (`clawd-mochi.local`): switch mode, adjust speed, change background / pen color, canvas, faces, PC Monitor, reminders, settings.
- **Screen button**: long-press 5 s to enter provisioning; tap to dismiss a reminder pop-up.
- Encoder mode-switching / touch gestures are planned for v0.3.0.

## Companion services

- **PC Monitor**: PC-side Python service, see [`pc_monitor/README.md`](./pc_monitor/README.md).
- **Claude Code status link**: configure Claude Code hooks, see [`cc_hooks/README.en.md`](./cc_hooks/README.en.md).

## Credits

Tribute to original author [@yousifamanuel](https://github.com/yousifamanuel). This is an independent fan project, not officially affiliated with Anthropic.

## License

[MIT](./LICENSE)
