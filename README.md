# clawd-mochi-m5dial

> [clawd-mochi](https://github.com/yousifamanuel/clawd-mochi) 的 M5Dial 重制版。
> 桌面伴侣硬件：圆屏 + 旋钮 + 触摸 + WiFi 控制。

English: [README.en.md](./README.en.md)

## 硬件

- [M5Stack M5Dial](https://docs.m5stack.com/zh_CN/core/M5Dial)（ESP32-S3 + 1.28" 圆形触摸屏 + 旋钮 + 蜂鸣器）
- USB-C 线缆

## 构建

需要 [PlatformIO Core](https://platformio.org/install/cli) 或 VSCode + PlatformIO IDE 扩展。

```pwsh
pio run                      # 编译
pio run -t upload            # 烧录到 M5Dial
pio run -t uploadfs          # 上传 Web 静态资源到 LittleFS
pio device monitor -b 115200 # 串口监视
```

## 控制

- **旋钮**：切换显示模式
- **触摸**：触发眨眼 / 画板绘图
- **WiFi**：手机连热点 `ClaWD-Mochi`（密码 `clawd1234`），浏览器开 `http://192.168.4.1`

## 致敬

向原作者 [@yousifamanuel](https://github.com/yousifamanuel) 致敬。本项目为独立 fan project，与 Anthropic 无官方关联。

## 许可

[MIT](./LICENSE)
