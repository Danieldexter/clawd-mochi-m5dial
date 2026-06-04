# CLAUDE.md — clawd-mochi (M5Dial Edition)

> 这份文档是供 Claude Code（以及人类开发者）阅读的项目上下文。
> 它不是 README——它告诉你**为什么这样写**以及**写代码时要避开哪些坑**。
> 新开会话时请先读这份，再读代码。

---

## 1. 行为准则 / Behavioral Guidelines for Claude

> **目的**：减少 LLM 写代码时常见的失误。本节专门写给 Claude（及任何 LLM 协作者）阅读，**优先级高于本文档其他内容**。
> **Tradeoff**：这些准则偏向"谨慎"而非"快速"。琐碎任务请用判断力，不必死板套用。

### 1.1 Think Before Coding / 动手前先思考
**不要假设。不要藏起困惑。把权衡摆到台面上。**

开始实现之前：
- **明确写出你的假设**——不确定就问。
- 如果有多种合理解释，**全部列出**，不要默默替用户选一种。
- 如果有更简单的做法，**说出来**。必要时反驳用户的方案。
- 遇到不明白的地方，**停下**。指出具体哪里不明白。然后问。

### 1.2 Simplicity First / 简洁优先
**只写解决问题所需的最少代码。不写任何投机性代码。**

- 不加用户没要求的功能。
- 不为只用一次的代码做抽象。
- 不做没人要求的"灵活性"或"可配置性"。
- 不为不可能发生的场景写错误处理。
- 如果你写了 200 行，而 50 行就能搞定 —— **重写**。

自检：一位资深工程师会不会说这"过度设计"？如果会，简化。

### 1.3 Surgical Changes / 外科手术式改动
**只动你必须动的地方。只清理你自己造成的混乱。**

编辑现有代码时：
- 不要"顺手优化"相邻的代码、注释或格式。
- 不要重构没坏的东西。
- 沿用现有风格，即便你不认可。
- 注意到无关的死代码，**口头提一句即可，不要删**。

当你的改动让某些代码变成孤儿：
- 删除**你的改动**导致不再被引用的 import / 变量 / 函数。
- **不要**顺带删除之前就已经存在的死代码（除非被明确要求）。

检验标准：**每一行变更都能直接追溯到用户的请求**。

### 1.4 Goal-Driven Execution / 目标驱动执行
**先定义"完成"的判据，然后循环到验证通过为止。**

把任务转成可验证的目标：
- "加校验" → "为非法输入写测试，让测试通过"
- "修 bug" → "写一个能复现 bug 的测试，让它通过"
- "重构 X" → "改之前测试通过，改之后测试仍通过"

多步任务先简述计划：
```
1. [步骤] → 验证：[检查]
2. [步骤] → 验证：[检查]
3. [步骤] → 验证：[检查]
```

强的成功判据让 Claude 可以独立循环。弱的判据（"让它能用"）会逼你不断回头索要澄清。

### 1.5 重构不留旧版兼容 / No Backward Compat Unless Asked
**用户没明确说"保兼容"，重构 = 全量替换。**
- 不写 v1/v2 双轨；不留 `// 旧逻辑保留` 之类的注释。
- 不留 deprecated wrapper / 过渡函数；所有调用点同步改成新逻辑。

### 1.6 默认并行 / 不阻塞 / Non-Blocking by Default
**主循环里 `M5Dial.update()` 不能被卡。**
- 网络 IO、Flash 写入、SPI 大块传输等耗时操作放 FreeRTOS task 或异步回调。
- Web 服务用 `AsyncWebServer`（§4 已选型），不要用阻塞型 WebServer。
- 触摸 / 编码器 ISR 只 set flag，主循环消费。

### 1.7 单文件 ≤ 3000 行 / File Size Cap
- 任何源文件接近 3000 行必须拆分（按 mode / 模块切，参见 §7 目录布局）。
- 写的过程中预估，不要先写完再切。

### 这些准则起效的信号
- diff 里**少了**不必要的改动；
- 因过度设计而重写的次数**减少**；
- 澄清问题**在动手前**提出，而不是在犯错之后。

---

## 2. Project Overview

本项目是开源项目 [clawd-mochi](https://github.com/yousifamanuel/clawd-mochi) 的 **M5Dial 重制版**。

**原项目**：基于 ESP32-C3 + ST7789 1.54" 方形屏的桌面伴侣，通过 WiFi AP + Web UI 控制，显示像素风格表情动画（眼睛、笑脸、Claude Code 文字、画板）。

**本项目目标**：将相同的视觉精神移植到 **M5Dial**（ESP32-S3 + 1.28" 圆形 GC9A01 触摸屏 + 旋转编码器 + 蜂鸣器），并利用 M5Dial 多出的物理交互通道，让设备既能本地操作也能 Web 控制。

**致敬声明 / Disclaimer**: This is an independent fan project. 与 Anthropic 公司无官方关联，仅作为对 Claude Code 吉祥物 "Clawd" 的致敬。代码遵循原项目 MIT 协议；3D 模型与媒体资源遵循 CC BY-NC-SA 4.0。

**与原项目的本质差异**：硬件几何（方→圆）、MCU 架构（RISC-V→Xtensa）、显示驱动、GPIO 映射、交互维度都不同，因此**不能直接复用原仓库的 `.ino` 代码**，本项目从零重写为标准 C++ 多文件结构。

---

## 3. Hardware Target — M5Dial (SKU K130)

下表是后续所有引脚配置的 Single Source of Truth。引脚号变更必须先改这里。

### MCU & Memory
| 项目 | 规格 |
|------|------|
| SoC | ESP32-S3FN8（Xtensa LX7 双核 @ 240 MHz） |
| Flash | 8 MB |
| 基础模组 | M5Stack StampS3 |

### Display & Touch
| 项目 | 规格 / 引脚 |
|------|-------------|
| 屏幕 | 1.28" 圆形 TFT，**GC9A01** SPI 驱动，240×240 |
| 触摸 | **FT3267**，I²C 地址 `0x38` |
| Display pins | RS=G4, MOSI=G5, SCK=G6, CS=G7, RESET=G8, BL=G9 |
| Touch pins | SDA=G11, SCL=G12, INT=G14 |

### Input
| 项目 | 规格 / 引脚 |
|------|-------------|
| 旋转编码器 | 16 定位 / 64 脉冲每圈，B=G40, A=G41 |
| 屏下按钮 | WAKE（M5Dial 库通过 `M5Dial.BtnA` 暴露） |
| 复位 / 下载 | RST 物理键 + StampS3 的 G0（按住 G0 + 按 RST 进下载模式） |

### Other Modules
| 模块 | 规格 / 引脚 / 备注 |
|------|--------------------|
| 蜂鸣器 | 80 dB，pin **G3**（PWM/ledc） |
| RFID | WS1850S，13.56 MHz，I²C `0x28`，RST=G8（与屏幕共用，注意复用），IRQ=G10 — **v1 不使用** |
| RTC | BM8563，I²C `0x51`，支持定时唤醒 — **v1 不使用** |
| **HOLD pin** | **G46** — 必须程序拉高，否则板子初始化后会立刻断电 |

### Power
- USB-C：5 V
- 锂电池：3.7 V，1.25 mm-2P 座子，带充电电路
- DC 端子：6 ~ 36 V 宽压输入
- 休眠电流：4.2 V @ 1.9 µA（深度休眠）

### Ports (HY2.0-4P)
| 端口 | 颜色 | 引脚 |
|------|------|------|
| PORT.A | 红 | GND / 5V / **G13 (SDA)** / **G15 (SCL)** — I²C 扩展 |
| PORT.B | 黑 | GND / 5V / G2 / G1 — 通用 GPIO |

### 物理尺寸
51.0 × 51.0 × 32.3 mm，46.3 g，工作温度 0–40 °C。

---

## 4. Architecture & Toolchain

### 构建系统
- **PlatformIO** + Arduino framework
- `platformio.ini` target: `m5stack-stamps3`（M5Dial 基于 StampS3 模组）
- 文件系统：**LittleFS**（用于 Web 静态资源）

### 依赖库（写入 `platformio.ini`）
```ini
lib_deps =
    m5stack/M5Dial            ; 自动拉入 M5Unified + M5GFX/LovyanGFX
    ESP32Async/ESPAsyncWebServer    ; Arduino Core 3.x 起须用 ESP32Async fork（me-no-dev 已停维护）
    ESP32Async/AsyncTCP
    bblanchon/ArduinoJson
```

### 为什么不用原项目的 Adafruit GFX/ST7789
1. **驱动不兼容**：M5Dial 屏幕是 GC9A01，Adafruit ST7789 库不支持
2. **性能**：M5GFX 提供硬件加速的 sprite 双缓冲，对圆屏渲染更友好
3. **触摸集成**：M5Dial 库已经把 FT3267 触摸和编码器封装好，避免重新写驱动

### 整体架构（事件驱动状态机）
```
                ┌──────────────────────┐
   Encoder ───▶│                      │
   Touch    ──▶│   InputDispatcher    │──▶ ModeManager ──▶ Current Mode ──▶ Render
   Web WS   ──▶│                      │            │
                └──────────────────────┘            │
                                                    ▼
                                              Audio (beeper)
```

- 主循环 `loop()` 只做：`M5Dial.update()` → 拉取输入事件 → 派发给当前模式 → 调用模式的 `tick()` 渲染
- 每种模式实现统一接口 `IMode { onEnter(); onExit(); tick(uint32_t now); onInput(Event); }`
- Web 控制通过 WebSocket 把消息转成同样的 `Event`，与硬件输入走同一条管道

---

## 5. Feature Mapping — 原项目 → M5Dial 版

| 原项目特性 | M5Dial 适配策略 |
|-----------|----------------|
| Normal eyes（像素方眼，摇摆 + 眨眼） | 保留视觉风格，但 X/Y 坐标加圆形剪裁；眼间距从原 `EYE_GAP` 收窄约 20% 以适配 1.28" |
| Squish eyes（`> <` 眯眼笑） | 保留，改用 M5GFX 矢量描边 + 圆角，避免位图缩放锯齿 |
| Claude Code 模式（终端文字滚动） | 文字内容限制在以中心为圆心的内接矩形（约 170 × 170 px）内滚动 |
| Canvas 绘画 | 触摸屏直接绘制 + Web 同步；笔触按圆形 mask 裁剪；颜色/笔粗 Web 控制 |
| Web AP `ClaWD-Mochi` / `clawd1234` | v0.1.0 用此常驻控制 AP；**v0.2.0 Phase 9 起取消**——改为配网开放 AP `Clawd-Mochi-Setup`（**按需进入**：默认开机进设备界面，BtnA 长按或 Settings「Reset WiFi」才进配网）→ 存 NVS → 重启进 STA，正常运行经 `clawd-mochi.local` 访问（见 §9）。AsyncWebServer + WebSocket 保留 |
| 速度滑块、背景色、笔色 | 保留，Web 面板 UI 重新设计为圆形预览 |
| 显示开关（背光） | 保留，控制 G9 的 PWM 占空比，0 = 全黑但 MCU 仍运行 |
| **新增**：旋钮切模式 | 旋转 = 切换 4 种模式；按下 = 进入/退出当前模式的子菜单 |
| **新增**：触摸手势 | Tap = 触发眨眼或笑；Long-press = 强制切下一模式；Drag（仅 Canvas）= 画线 |
| **新增**：蜂鸣器音效 | 模式切换 80 ms 短鸣；Canvas 落笔 20 ms 极短"嗒"；Web 客户端连接成功上扬双音 |
| **新增**：Claude Code 联动状态灯（本项目原创） | hooks 直推 `GET /cc?s={working\|waiting\|idle}` → 设备用 Clawd 表情反映会话态（思考绿点 / 待确认 `!`黄环 / 待命摇摆眼）；非 canvas 表情模式对话时**自动切入**，canvas 不打断；详见 `cc_hooks/`（受 [DemoJj/claude-code-traffic-light](https://github.com/DemoJj/claude-code-traffic-light) 启发）|

### mumuer1024 二次开发功能（v0.2.0 范围）

参考 [mumuer1024/clawd-mochi-public](https://github.com/mumuer1024/clawd-mochi-public)（原项目的二次开发版）。下表为新增功能与 M5Dial 适配策略；原项目 4 mode 保持不变作为 v0.1.0 基础。

| 二次开发新功能 | M5Dial 适配策略 |
|--------------|----------------|
| **WiFi AP 配网** — 首启无凭据建 `Clawd-Mochi-Setup` 开放热点 → 用户填 SSID / 密码 / PC IP → 存 Preferences (NVS) → 重启 STA | M5Dial 版**默认开机进设备界面**（单机可玩），配网为**按需**：M5Dial 无 GPIO 5，**进配网改用 BtnA 长按 ≥ 5s**（屏下 WAKE，`setHoldThresh(5000)`+`wasHold()`，非破坏性：保留已存凭据）或 Settings「Reset WiFi」；二者经 `requestSetupMode()` 置 NVS 标志 + 重启进入 |
| **PC Monitor mode** — 设备每 50s 调 `http://<PC_IP>:8080/stats.json`，显示 load / mem / temp / uptime / 时钟 | 圆形布局：中心大字 CPU%，下方一行 mem%，左右弧形条带 temp / uptime；时钟数字居中。文字遵守 §6 安全区（半径 ≤ 110） |
| **Reminder 系统** — ≤ 5 条定时提醒，触发时全屏消息 30s 后回原 mode；REST CRUD | 提醒列表存 NVS；定时器走 FreeRTOS task；触发通过 ModeManager 切到内置 reminder overlay 模式，超时自动 setMode 回原 |
| **Face System** — 7 静 + 10 动共 17 表情，原版为 6×6 像素块代码 | **不照搬原 `faces_code.h`**；只保留 `face_wuyu` / `anim_smile` 等命令语义，M5Dial 版重新设计为圆屏友好的矢量/块混合表情；4bpp 离屏渲染；单 mode `face_show` 承载稳定 face key |
| **Settings 页** — Web 改 PC IP / 重置 WiFi 配置 / 重启 | 复用 AsyncWebServer 多挂一个静态页 + 几个 WS message type；Reset WiFi 调 `Preferences.clear()` 后 `ESP.restart()` |
| **PC Monitor Python 服务** — Flask + pystray + autostart（Win/Mac/Linux）+ PyInstaller 打包 | 整套 `pc_monitor/` 目录从 mumuer1024 原样搬入项目根；端口 8080，API `GET /stats.json`；与固件解耦 |

> **API 风格决策**：mumuer1024 上述功能用 HTTP GET 命令实现（`/cmd?k=` / `/reminder` CRUD 等）；本项目按 CLAUDE.md §4 原设计统一走 **AsyncWebServer + WebSocket**——每个 HTTP endpoint 翻译成一个 WS message type（如 `{type:"reminder_add", time, msg}`）；好处是 reminder 触发 / monitor 刷新可由设备主动推送给 web 客户端，无需轮询。

---

## 6. UI Adaptation for Round Display

圆屏不是带了圆角的方屏。以下约束写代码时**必须**遵守：

### 极简风优先 / Minimalist Design
- 不放多余说明文字。像素动画 / 图标 / 颜色变化能自解释的，**不加文本说明**。
- 不暴露开发术语（"buffer"、"sprite"、"WebSocket connected"、IP 地址）给终端用户——这些只能出现在串口 log。
- 错误反馈用简短视觉信号（红色短闪 + 蜂鸣器单短鸣），不堆字。

### 安全区
所有有意义的视觉元素必须落在 **以屏幕中心 (120, 120) 为圆心、半径 110 px 的圆**内。靠边的 10 px 留给抗锯齿与制造容差。

### 字体
- ❌ 不要用 Adafruit GFX 风格的 6×8 / 8×8 位图字体——圆屏上像素感太重
- ✅ 使用 M5GFX 内置矢量/抗锯齿字体，如 `&fonts::lgfxJapanGothic_20` 或 `&fonts::FreeSans12pt7b`
- Claude Code 模式可以保留等宽位图字体（致敬终端审美），但限制在内接矩形内

### 眼睛布局
- 水平居中，垂直**略偏上**（中心 Y ≈ 110 而不是 120），视觉上避免"下巴空"
- 眨眼时眼睑从上下两侧合拢，不要用方屏常见的 scale-Y 缩放（圆屏边缘会切掉信息）

### Canvas 模式
- 在 PSRAM/RAM 中维护一个 `LGFX_Sprite` 240×240 作为后备 buffer
- 触摸或 Web 写入像素前判断：`(x-120)² + (y-120)² ≤ 110²`，否则丢弃
- 每帧只 `pushSprite()` 一次，不要每画一笔就刷屏

### 过渡动画
- ✅ 适合圆屏：fade（亮度）、radial wipe（从圆心扩散）、rotation
- ❌ 避免：horizontal slide、vertical scroll 整屏切换（圆屏两侧会露出未定义内容）

---

## 7. File / Directory Layout

```
clawd-mochi/
├── CLAUDE.md                  # 本文档
├── README.md                  # 用户向（中文）
├── README.en.md               # 用户向（英文）
├── CHANGELOG.md
├── LICENSE
├── platformio.ini             # 构建配置
├── src/
│   ├── main.cpp               # setup() / loop()，调用 ModeManager
│   ├── config.h               # 跨 mode 常量（圆屏几何、AP 凭据等）
│   ├── mode_manager.{h,cpp}   # 模式注册、切换、事件分发
│   ├── modes/
│   │   ├── i_mode.h           # 模式接口 IMode
│   │   ├── eyes_normal.{h,cpp}     # v0.1.0
│   │   ├── eyes_squish.{h,cpp}     # v0.1.0
│   │   ├── claude_code.{h,cpp}     # v0.1.0
│   │   ├── canvas.{h,cpp}          # v0.1.0
│   │   ├── claude_status.{h,cpp}   # v0.2.0：Claude Code 联动状态模式（思考/待确认/待命，自绘表情）
│   │   ├── pc_monitor.{h,cpp}      # v0.2.0：CPU/内存/温度/uptime 圆形面板
│   │   └── face_show.{h,cpp}       # v0.2.0：单 mode + face key 承载 17 表情
│   ├── faces/
│   │   └── faces_data.{h,cpp}      # v0.2.0：17 表情 FaceSpec 注册表 + 圆屏重设绘制
│   ├── services/
│   │   ├── provisioning.{h,cpp}    # v0.2.0：首启 AP 配网 + NVS 存储
│   │   └── reminder.{h,cpp}        # v0.2.0：≤5 条提醒 + 定时器
│   ├── input/
│   │   ├── encoder.{h,cpp}         # v0.3.0：编码器去抖与事件
│   │   └── touch.{h,cpp}           # v0.3.0：tap / long-press / drag
│   ├── web/
│   │   ├── ap_server.{h,cpp}       # AP + AsyncWebServer + LittleFS
│   │   ├── ws_protocol.h           # v0.1.0：WS 消息类型（mode/stroke/state/reminder/face/monitor）
│   │   └── ws_handler.{h,cpp}      # WebSocket ↔ Event 双向转换
│   └── audio/
│       └── beeper.{h,cpp}          # v0.3.0：LEDC 驱 G3 蜂鸣器
├── data/                      # 通过 `pio run -t uploadfs` 烧到 LittleFS
│   ├── index.html             # v0.1.0：主控制面板
│   ├── setup.html             # v0.2.0：首启配网页（AP 模式独占）
│   ├── settings.html          # v0.2.0：改 PC IP / 重置 WiFi
│   ├── style.css
│   └── app.js                 # 圆形预览 canvas + WS 客户端
├── pc_monitor/                # v0.2.0：PC 端 Python Flask 服务（从 mumuer1024 移植）
│   ├── pc_monitor.py
│   ├── config.json
│   ├── requirements.txt
│   ├── start.bat / start.sh
│   ├── icon/
│   └── README.md
├── cc_hooks/                  # v0.2.0：Claude Code 联动 hooks 示例 + 说明（PC 端，纯 curl，零 Python）
│   ├── settings.example.json
│   └── README.md
└── docs/
    ├── pinout.md              # M5Dial 引脚速查（从 §3 摘出）
    └── workflow.md            # 接手文档（git 忽略）
```

---

## 8. Build & Flash Workflow

```bash
# 编译
pio run

# 烧录固件（M5Dial 通过 USB-C 自动进 DFU；失败则按住 G0 后按 RST）
pio run -t upload

# 上传 Web 静态资源到 LittleFS
pio run -t uploadfs

# 串口监视（注意 USB CDC On Boot，无需驱动）
pio device monitor -b 115200
```

PowerShell 用户注意：PowerShell 5.1 不支持 `&&` 链式调用，命令分开跑或用 `; if ($?) { ... }`。

---

## 9. Critical Constraints / Gotchas

写代码或排查 bug 时，先看这一节：

### 必须做的事
- **G46 HOLD pin 必须在 `setup()` 早期 `digitalWrite(46, HIGH)`**——否则板子开机后立刻断电
- 实际上 `M5Dial.begin()` 内部已**无条件**处理 G46（通过 M5Unified Power 模块），但**任何绕过 M5Dial 库的代码**（如纯 ESP-IDF demo）必须自己处理。`begin(cfg, enableEncoder, enableRFID)` 的后两个参数与 HOLD 无关
- `M5Dial.update()` 必须每个 `loop()` 调一次，否则编码器与触摸事件不更新

### 不要做的事
- ❌ **插 USB-C 上电时切勿按住 G0**——长按 G0 + 通电 = ESP32-S3 直接停在 ROM bootloader 等下载命令，**完全不运行用户代码**。症状：烧录成功但 Serial 几乎沉默（仅 `ESP-ROM:esp32s3-20210327` 一行 banner，之后什么都没有）。正常上电就别按 G0；只有进 DFU 烧录失败时才用"按住 G0 → 按一下 RST → 松 RST → 松 G0"手势
- ❌ **不要在中断 ISR 里调用任何 M5GFX / Serial / Wire / log_x() 函数** —— 这些不是 ISR-safe，会随机崩溃。ISR 里只 set flag，主循环处理
- ❌ 不要把 SPI 频率拉到 80 MHz 以上——GC9A01 实测 40 MHz 稳定，60 MHz 边缘场景下花屏
- ❌ 不要在**有动画的正常运行态**同时启用 WiFi STA + AP——会显著掉帧。v0.2.0 起网络模型：**默认开机进设备界面（动画）**——有凭据则后台 **STA-only** 连接供 web 控制（`clawd-mochi.local`），无凭据/连接失败则**单机运行**（无网，不自动进配网）；**配网态 AP_STA**（开放 AP + captive portal，STA 空闲仅供 `/scan`，无动画渲染）为**按需进入**——BtnA 长按 5s 或 Settings「Reset WiFi」经 `requestSetupMode()` 置 NVS 标志后重启进入（标志启动时读后即清，一次性）。二者由重启切换，永不在有动画时共存。`begin(setup_mode)` 二选一
- ❌ 不要尝试 `#include` 原 clawd-mochi 的 `.ino` 文件——驱动栈完全不同，连引脚常量都对不上

### 容易踩的隐式假设
- 圆屏的 framebuffer **仍是 240×240 方形**，写到角落的像素物理上看不到但仍然消耗 SPI 带宽。画板等大数据传输前主动按圆形裁剪
- 触摸坐标原点在左上角 (0, 0)，旋钮顺时针 = encoder 值递增
- M5Dial 屏幕实际可视区域已校准，触摸坐标与屏幕坐标 1:1，不需要变换矩阵
- RFID 的 RST 引脚是 G8，与屏幕 RESET 复用——所以 v1 不启用 RFID 才安全，未来要用必须重新设计屏幕复位时序
- 蜂鸣器用 `ledcWriteTone(channel, freq)`，不要用 `tone()`（ESP32 Arduino 上 `tone()` 实现不稳定）

---

## 10. Out of Scope

收纳"**永久不做**"与"**v0.3.0+ M5Dial 增强候选**"两类，避免 scope creep；详细 phase 排布见 `docs/workflow.md`。

### 永久不做（明确划出）
- [ ] OTA 固件升级
- [ ] MQTT / Home Assistant 集成
- [ ] 多语言切换（v1 全英文 + 部分中文标签即可）
- [ ] 3D 外壳设计（M5Dial 自带外壳，无需重新建模）
- [ ] 多设备 mesh / BLE 通信

### v0.3.0+ 候选（M5Dial 硬件增强专属）
- [ ] RFID 卡片触发表情 / 动画（需重新设计屏幕 / RFID RST 复用时序）
- [ ] RTC 闹钟联动 Reminder 系统
- [ ] 电池电量显示与深度休眠唤醒
- [ ] 编码器 / 触摸完整替代 Web 控制（脱机操作）

这些条目记录在此是为了未来回看时**有意识地**决定要不要加，而不是默默被忘掉。

---

## 11. 协作规范 / Collaboration Conventions

> 项目级协作约定。Claude ↔ 用户的工作流规则。

### 11.1 回答风格 / Response Style for Claude
- 不写开场白（"好的，这是..."）和结束语（"如果你要，我可以..."），直接给结论与代码。
- 修改完成后用表格列"改动前 vs 改动后"。
- 结论必须有完整代码逻辑链支撑，不靠猜测；引用具体行号 / 文件名。
- 不滥用 Markdown 装饰，普通陈述用纯文本。

### 11.2 本地环境 / Local Environment
- 主机：Windows 11 + UTF-8 + **PowerShell 7**。
- 命令优先 PS 7 原生语法；避免 WSL / `rg` / `grep` / `find` / `cat` 等 Linux 工具。
- 文件搜索用 Claude 内置 Glob / Grep 工具，或 `Get-ChildItem -Recurse`。

### 11.3 Git 提交规范 / Commit Convention
- 格式：`xxxx(xx):中文描述`
  - 例：`feat(ui):新增 squish 眨眼动画` / `fix(canvas):修正圆形 mask 边缘漏笔` / `chore(init):项目初始化`
- 沿用此格式，**不**引入英文 Conventional Commits 全套规范（虽然形态相似）。

### 11.4 Git 操作安全 / Git Safety
- 执行 `reset` / `checkout` / 任何回滚操作前，**必须先 `git status` 确认无未提交修改**。
- 如有未提交改动，**先告知用户**并询问处理方式（stash / commit / discard），不要默认覆盖。

### 11.5 开发交接 / Handover via workflow.md
- `docs/workflow.md` 是接手文档：项目当前架构 + 20 字内修改记录（含时间戳）。
- **每次完成任务后更新**该文档，删除过时信息。
- **`docs/workflow.md` 必须加入 `.gitignore`**（本地接力，不入版本控制）。
- 新会话先读 `docs/workflow.md` 了解未完成项；若不存在，需先创建。
- 复杂长任务可启用 subagents 协调；主线程只下发任务与汇总结果。

---

## 12. References

### 项目相关
- 原项目仓库：https://github.com/yousifamanuel/clawd-mochi
- **二次开发参考**：https://github.com/mumuer1024/clawd-mochi-public （AP 配网 / PC Monitor / Reminder / Face System / Settings 的实现参照，v0.2.0 范围）
- 原项目 MakerWorld（3D 模型）：见原仓库 README

### 硬件
- M5Dial 官方文档（中文）：https://docs.m5stack.com/zh_CN/core/M5Dial
- M5Dial 官方文档（英文）：https://docs.m5stack.com/en/core/M5Dial
- M5Dial 出厂固件源码：https://github.com/m5stack/M5Dial-UserDemo
- M5Dial GUI Demo：https://github.com/Gitshaoxiang/M5Dial-Gui-Demo

### 库
- M5Dial Arduino 库：https://github.com/m5stack/M5Dial
- M5Unified（核心抽象层）：https://github.com/m5stack/M5Unified
- M5GFX（显示库）：https://github.com/m5stack/M5GFX
- LovyanGFX（M5GFX 上游）：https://github.com/lovyan03/LovyanGFX
- ESPAsyncWebServer：https://github.com/ESP32Async/ESPAsyncWebServer

### 工具
- PlatformIO 文档：https://docs.platformio.org/
- ESP32-S3 datasheet：https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
