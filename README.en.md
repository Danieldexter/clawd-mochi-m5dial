# clawd-mochi-m5dial

> M5Dial remix of [clawd-mochi](https://github.com/yousifamanuel/clawd-mochi).
> Desktop companion device: round display + rotary encoder + touch + Wi-Fi control.

中文：[README.md](./README.md)

## Hardware

- [M5Stack M5Dial](https://docs.m5stack.com/en/core/M5Dial) (ESP32-S3 + 1.28" round touch display + encoder + buzzer)
- USB-C cable

## Build

Requires [PlatformIO Core](https://platformio.org/install/cli) or VSCode + PlatformIO IDE extension.

```pwsh
pio run                      # compile
pio run -t upload            # flash to M5Dial
pio run -t uploadfs          # upload web assets to LittleFS
pio device monitor -b 115200 # serial monitor
```

## Controls

- **Encoder**: switch display mode
- **Touch**: trigger blinks / draw on canvas
- **Wi-Fi**: connect to AP `ClaWD-Mochi` (password `clawd1234`), open `http://192.168.4.1`

## Credits

Tribute to original author [@yousifamanuel](https://github.com/yousifamanuel). This is an independent fan project, not officially affiliated with Anthropic.

## License

[MIT](./LICENSE)
