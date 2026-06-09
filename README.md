# clawd-mochi-m5dial

> [clawd-mochi](https://github.com/yousifamanuel/clawd-mochi) 的 M5Dial 重制版。
> 桌面伴侣硬件：圆屏 + 旋钮 + 触摸 + WiFi 控制。

English: [README.en.md](./README.en.md)

## 硬件

- [M5Stack M5Dial](https://docs.m5stack.com/zh_CN/core/M5Dial)（ESP32-S3 + 1.28" 圆形触摸屏 + 旋钮 + 蜂鸣器）
- USB-C 线缆

## 功能

- **表情眼睛**：普通眼（摇摆 + 眨眼）、笑眼（`> <`）
- **Claude Code 终端**：滚动的终端文字动画
- **画板**：Web 上涂鸦，设备实时同步显示
- **Face System**：17 种圆屏表情，Web 下拉切换
- **Claude Code 联动**（本项目原创）：配好 hooks 后，设备表情实时反映会话状态——思考中 / 待确认 / 待命
- **PC Monitor**：显示电脑 CPU / 内存 / 显卡 / 网络 / 磁盘等信息（需在电脑上运行配套服务）
- **定时提醒**：最多 5 条每日提醒，到点全屏弹出，30 秒后自动返回
- **GIF 播放器**（v0.4.0）：Web 上传 GIF（≤240px / ≤512KB，最多 4 张图库），设备端解码循环播放、铺满圆屏；旋钮旋转切换图库，Web 可删除

### 演示

Claude Code 联动 —— 设备实时反映会话态：

<p align="center">
  <img src="docs/images/claude%20Link%20%E8%AE%BE%E5%A4%87%E6%BC%94%E7%A4%BA.jpg" width="320" alt="设备 Claude 联动演示">
</p>

PC Monitor —— 设备圆形面板：

<p align="center">
  <img src="docs/images/pc%20monitor%20%E8%AE%BE%E5%A4%87%E6%BC%94%E7%A4%BA.jpg" width="320" alt="设备 PC Monitor 演示">
</p>

Web 控制面板一览（浏览器经 `clawd-mochi.local` 访问）：

<p align="center">
  <img src="docs/images/web%20faces.png" width="190" alt="Web Faces 面板">
  <img src="docs/images/web%20canvas.png" width="190" alt="Web Canvas 面板">
  <img src="docs/images/web%20claude%20code.png" width="190" alt="Web Claude Code 面板">
  <img src="docs/images/WEB%20claude%20Link.png" width="190" alt="Web Claude Link 面板">
  <img src="docs/images/WEB%20PC%20Monitor.png" width="190" alt="Web PC Monitor 面板">
</p>

## 构建与烧录

需要 [PlatformIO Core](https://platformio.org/install/cli) 或 VSCode + PlatformIO IDE 扩展。

```pwsh
pio run                      # 编译
pio run -t upload            # 烧录固件
pio run -t uploadfs          # 上传 Web 资源到 LittleFS（改了 data/ 后必做）
pio device monitor -b 115200 # 串口监视
```

## 首次使用

1. 上电后直接进入设备界面，单机即可把玩，无需联网。
2. 要用 Web 控制：**长按屏下按钮 5 秒**进入配网——手机连开放热点 `Clawd-Mochi-Setup`，配网页会自动弹出（或扫屏幕上的二维码），填入家里的 WiFi 与电脑 IP，保存后设备重启联网。
3. 之后在同一局域网下，浏览器打开 **`http://clawd-mochi.local`** 即可控制。

## 控制

- **Web 面板**（`clawd-mochi.local`）：切换模式、调速度、改背景 / 笔色、画板、切表情、PC Monitor、提醒、Settings。
- **屏下按钮**：长按 5 秒进配网；提醒弹出时轻点关闭。
- 旋钮切模式 / 触摸手势计划于 v0.3.0。

## 配套服务

- **PC Monitor**：电脑端 Python 服务，见 [`pc_monitor/README.md`](./pc_monitor/README.md)。
- **Claude Code 联动**：配置 Claude Code hooks，见 [`cc_hooks/README.md`](./cc_hooks/README.md)。

## 致敬

向原作者 [@yousifamanuel](https://github.com/yousifamanuel) 致敬。本项目为独立 fan project，与 Anthropic 无官方关联。

## 许可

[MIT](./LICENSE)
