# Clawd Mochi PC Monitor

PC 端系统监控服务，为 Clawd Mochi 设备提供 **6 大分类**数据（CPU / 磁盘 / 显卡 / 主机 / 网络 / 流量）。

- **psutil**（跨平台、零特权）：CPU 占用/频率/核心、内存/swap、磁盘占用+读写速率、网络上下行+日流量、uptime、时钟。
- **LibreHardwareMonitor**（Windows，可选）：温度 / 风扇 / 显卡 / 功耗 —— psutil 在 Windows 上拿不到。放 DLL 进 `lib/` 启用，见 [`lib/README.md`](lib/README.md)；缺失则自动降级纯 psutil，缺的字段设备显 `--`。

## 快速开始

### 1. 安装依赖

```bash
pip install -r requirements.txt
```

### 2. 启动服务

**Windows:**
```cmd
双击 start.bat
```

**Linux/macOS:**
```bash
chmod +x start.sh
./start.sh
```

或直接运行：
```bash
python pc_monitor.py
```

启动后会显示系统托盘图标，服务运行在 `http://你的IP:8080`

### 3. 配网设置

在Clawd Mochi设备的配网页面或设置页面中，将PC IP设置为运行此脚本的电脑IP地址。

## PyInstaller打包

将脚本打包为独立可执行文件：

### Windows打包为.exe

```cmd
pip install pyinstaller pythonnet
pyinstaller --onefile --windowed --icon=icon/favicon.ico --name "ClawdMochiMonitor" ^
  --add-data "lib;lib" --add-data "icon;icon" ^
  --hidden-import clr --collect-all pythonnet --collect-all clr_loader ^
  pc_monitor.py
```

打包完成后，可执行文件位于 `dist/ClawdMochiMonitor.exe`

**注意**:
- 打包后开机自启功能会自动使用exe路径，无需手动设置。
- **必须用 64 位 Python 打包**（LHM 仅 x64）。`lib/` 里的 DLL 会随包发布、运行时解包到临时目录（脚本用 `sys._MEIPASS` 定位）。
- pythonnet + PyInstaller 组合较脆，**务必在一台干净的机器上验证冻结后的 exe** 能正常起 LHM（否则会静默退回纯 psutil）。

### Linux/macOS打包

```bash
pyinstaller --onefile --windowed --icon=icon/android-chrome-512x512.png --name "ClawdMochiMonitor" pc_monitor.py
```

## 开机自启

脚本支持跨平台开机自启功能（用户级别，无需管理员权限）：

### Windows
- 通过注册表 `HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run` 实现
- 打包为exe后点击托盘菜单"开机自启"即可启用

### macOS
- 通过 `~/Library/LaunchAgents/com.clawd-mochi.monitor.plist` 实现
- 点击托盘菜单切换状态

### Linux
- 通过 `~/.config/autostart/clawd-mochi-monitor.desktop` 实现
- 点击托盘菜单切换状态

## 硬件传感器后端（温度 / 风扇 / 显卡 / 功耗）

这些字段来自 **LibreHardwareMonitor (LHM)**，psutil 给不了：

1. 按 [`lib/README.md`](lib/README.md) 把 `LibreHardwareMonitorLib.dll` + `HidSharp.dll`（**64 位 / net472**）放进 `lib/`。
2. 用 **64 位 Python** 运行（LHM 仅 x64）。Win10/11 内置 .NET 4.7.2，无需额外装运行时。
3. **温度 / 主板 / 风扇**多数需**以管理员运行** + 安装 **PawnIO**（`winget install PawnIO`；LHM 0.9.4+ 用 PawnIO 取代被杀软误报的 WinRing0）。GPU 占用/显存、磁盘、网络通常免管理员。
4. 不装 DLL / 非 Windows → 自动纯 psutil，温度/风扇/显卡为 `null`（设备显 `--`），其余正常。

托盘菜单第三行会显示当前后端（`LHM(管理员)` / `LHM(非管理员·温度受限)` / `psutil(无 LHM)`）。

## 防火墙注意事项

### Windows防火墙

首次运行时，Windows防火墙可能弹出提示，请选择"允许访问"。

或手动添加规则：
```cmd
netsh advfirewall firewall add rule name="Clawd Mochi Monitor" dir=in action=allow protocol=tcp localport=8080
```

### Linux防火墙

如使用ufw：
```bash
sudo ufw allow 8080/tcp
```

如使用firewalld：
```bash
sudo firewall-cmd --add-port=8080/tcp --permanent
sudo firewall-cmd --reload
```

## 配置文件说明

`config.json` 配置项：

| 字段 | 说明 | 默认值 |
|------|------|--------|
| `port` | HTTP 服务监听端口 | 8080 |
| `refresh_interval` | 后台采样线程刷新间隔（秒） | 2 |
| `system_drive` | 磁盘占用统计的盘符；`null`=自动（Win `C:\` / 其它 `/`） | null |

修改端口后需重启服务生效。

## API接口

### `/stats.json`

返回嵌套 v2 契约。任一数值字段可为 `null`（设备显 `--`）；`av` 标各分类是否有数据。单位：温度 °C / 速率 MB/s / 显存 MB / 频率 MHz / 流量 MB。

```json
{
  "v": 2,
  "clk":  { "h": 14, "m": 30 },
  "av":   { "cpu": true, "gpu": true, "host": true, "disk": true, "net": true, "traf": true },
  "cpu":  { "ld": 37.5, "tp": 54,  "fq": 4200, "co": 16,  "pw": 65,  "fan": 1180 },
  "gpu":  { "ld": 12,   "tp": 48,  "vu": 2048, "vt": 8192,"ck": 1800,"pw": 40, "fan": 33 },
  "host": { "mem": 62.3,"swp": 18, "tp": 38,   "fan": 760,"up": "3d 5h" },
  "disk": { "use": 71.4,"rd": 12.5,"wr": 3.2,  "tp": 41 },
  "net":  { "up": 1.25, "dn": 8.40,"nic": "Ethernet" },
  "traf": { "up": 512.0,"dn": 4096.0 }
}
```

字段：`cpu` 占用/温度/频率/核心/功耗/风扇；`gpu` 占用/温度/显存用·总/核心频率/功耗/风扇；`host` 内存/swap/主板温度/机箱风扇/uptime；`disk` 占用/读/写/温度；`net` 上行/下行/网卡名；`traf` 当日上传/下载。

## 系统托盘菜单

- 状态：运行中 ✅
- IP：显示当前监听地址
- 开机自启：点击切换启用/禁用
- 退出：关闭服务

## 目录结构

```
pc_monitor/
├── pc_monitor.py        # 主脚本（psutil + LHM 后端 + 采样线程）
├── requirements.txt     # Python依赖（含 pythonnet, 仅 Windows）
├── config.json          # 配置文件
├── traffic.json         # 日流量基线（自动生成）
├── start.bat            # Windows启动脚本
├── start.sh             # Linux/macOS启动脚本
├── lib/                 # LibreHardwareMonitorLib.dll + HidSharp.dll（自行放置，见 lib/README.md）
├── icon/
│   ├── favicon.ico              # Windows托盘图标
│   └── android-chrome-512x512.png # Linux/macOS托盘图标
└── README.md            # 本说明文档
```

## 常见问题

**Q: 启动后无法访问API？**
A: 检查防火墙是否放行端口，确认IP地址正确。

**Q: 托盘图标不显示？**
A: 确保已安装 `pystray` 和 `pillow` 依赖。

**Q: 温度/风扇/显卡显示 `--`？**
A: 这些来自 LibreHardwareMonitor：确认 `lib/` 已放 DLL、用 64 位 Python 运行；温度/风扇多需**以管理员运行** + 安装 PawnIO（见上文「硬件传感器后端」）。非 Windows 无此数据。

## License

MIT License