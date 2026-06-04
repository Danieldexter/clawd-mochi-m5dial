# pc_monitor/lib/ — 放置 LibreHardwareMonitor 运行库

设备 PC Monitor 模式的「温度 / 风扇 / 显卡 / 功耗」数据来自 **LibreHardwareMonitor (LHM)**
（psutil 在 Windows 上拿不到这些）。把下面两个 DLL 放进**本目录**即可启用；缺失则服务自动降级为纯 psutil
（CPU 占用/频率、内存、磁盘占用+读写、网络上下行+流量、uptime 仍正常，温度/风扇/GPU 显示 `--`）。

需要的文件：

```
pc_monitor/lib/
├── LibreHardwareMonitorLib.dll   # 必需
└── HidSharp.dll                  # LHM 的依赖（同包发布）
```

## 获取方式

1. 到 LibreHardwareMonitor Releases 下载：https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases
   解压后从程序目录取 `LibreHardwareMonitorLib.dll` + `HidSharp.dll`。
   （或从 NuGet 包 `LibreHardwareMonitorLib` 的 `lib/net472/` 取，并自行补 `HidSharp.dll`。）
2. **必须用 64 位**版本（LHM 仅 x64；32 位 Python 会 `BadImageFormatException`）。
3. 选 **net472** 目标 DLL —— Win10/11 内置 .NET Framework 4.7.2，免装运行时。

## 运行须知

- **温度 / 主板 / 风扇**多数需要**以管理员运行** + 安装 **PawnIO**（LHM 0.9.4+ 用 PawnIO 取代被杀软误报的 WinRing0）：
  `winget install PawnIO`。GPU 占用/显存、磁盘、网络通常无需管理员。
- 不装本目录 DLL 也能用 —— 只是少了温度/风扇/GPU。

## License 提示

LibreHardwareMonitorLib 以 MPL-2.0 发布；这些 DLL **不**纳入本仓库版本控制（见 `.gitignore`），请自行下载放置。
