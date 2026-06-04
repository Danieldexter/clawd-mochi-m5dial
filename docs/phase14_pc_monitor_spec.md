# Phase 14 / 14b — PC Monitor mode 设计说明

> 6 大分类圆屏卡片（CPU / Disk / GPU / Host / Network / Traffic），默认轮询切换，web 配单显/轮询/间隔/启用分类。
> 深色基底 + 每类强调色 + 弧形仪表 / 进度条 / 数值混排。出站 HTTP 走常驻 FreeRTOS task（§1.6），渲染 4bpp 离屏缓冲（仿 face_show）。
> 代码：`src/modes/pc_monitor.{h,cpp}`；PC 服务：`pc_monitor/pc_monitor.py`（端口 8080，psutil + LibreHardwareMonitor）。

## 1. 数据来源与 stats.json v2 契约

PC 服务两层后端：**psutil**（跨平台、零特权）给 CPU 占用/频率/核心、内存/swap、磁盘占用+读写、网络上下行+日流量、uptime、时钟；**LibreHardwareMonitor**（Windows 可选）补 温度/风扇/GPU/功耗（psutil 在 Win 上拿不到）。缺/不可用字段输出 `null`，设备显 `--`；`av` map 标各分类是否有数据（无独显/无 LHM → `gpu:false`，该卡轮询时跳过）。

```json
{ "v":2, "clk":{"h":14,"m":30},
  "av":{"cpu":true,"gpu":true,"host":true,"disk":true,"net":true,"traf":true},
  "cpu":{"ld":37.5,"tp":54,"fq":4200,"co":16,"pw":65,"fan":1180},
  "gpu":{"ld":12,"tp":48,"vu":2048,"vt":8192,"ck":1800,"pw":40,"fan":33},
  "host":{"mem":62.3,"swp":18,"tp":38,"fan":760,"up":"3d 5h"},
  "disk":{"use":71.4,"rd":12.5,"wr":3.2,"tp":41},
  "net":{"up":1.25,"dn":8.40,"nic":"Ethernet"},
  "traf":{"up":512.0,"dn":4096.0} }
```

单位：温度 °C / 速率 MB/s / 显存 MB / 频率 MHz / 流量 MB。设备解析（`pc_monitor.cpp parseAndStore`）用 `jnum() = (v | NAN)`：缺/null/非数 → `NAN` 哨兵；`int co` 缺省 -1；字符串 `up`/`nic` 缺省空。时钟取自 PC，**无需 NTP/RTC**。

> 分类索引/位序 = `ws_protocol::kMonCatNames` = `{cpu,disk,gpu,host,net,traf}`，与 `pc_monitor.cpp kCatWire/kCatTitle`、`state::MonitorCfg.enabled_mask` 三处保持一致。

## 2. 圆屏布局（§6 安全圈 r≤110，圆心 120,120；坐标上机微调）

深色基底 `#1c1c20`(`kBgDark`)。固定调色板槽 `kInk`奶白0xFF38 / `kWhite`纯白 / `kDim`浅灰0xBDF7 / `kTrack`深灰0x39C8；`kAccent` **逐卡** `setPal565` 重涂。
每类强调色（RGB565，上机可调）：CPU `#ff4000` · Disk `#F2B544` · GPU `#A47CF0` · Host `#5AA0E0` · Net `#3FB984` · Traffic `#4ECDC4`。

| 元素 | 锚点 / datum | 字体 | 调色板 |
|---|---|---|---|
| 时钟 `HH:MM` | (120,22) top_center | FreeSans9 | kWhite（离线→kDim） |
| 分类标题 | (120,46) top_center | FreeSans12 | **kAccent** |
| rim 弧仪表（主指标%） | 圆心环 r0=100 r1=108，起 135° 扫 270°（底留 90° 给圆点） | — | 底环 kTrack + accent∝pct |
| 中心大数值 | (120,98) middle_center | FreeSansBold24 | kInk |
| 大数值标签 | (120,126) middle_center | FreeSans9 | kDim |
| 次级进度条 | x55 y152 w130 h12 圆角 | — | 底槽 kTrack + accent∝pct |
| 读数行（有 bar 卡） | (120,176) | FreeSans9 | kInk |
| 双读数行（无 bar 卡） | (120,156)/(120,184) | FreeSans9 | kInk |
| 圆点指示 | (居中,204) 启用∧可用卡数；当前 accent 其余 kDim | — | — |

> 温度一律 `%dC`（不用 ° 字形——Adafruit 字库无）；分隔用双空格（全 ASCII）。橙红/紫/金等 accent + 奶白数值在深底上高对比。

### 每卡内容（弧=主%，bar=次%「进度条」，row=速率/温度等）
| 卡 | rim 弧 | 中心大字 | 进度条 | 读数行 |
|---|---|---|---|---|
| CPU | load% | `47%` LOAD | 温度 0-100°C | `54C  4.2GHz  65W` |
| GPU | load% | `12%` LOAD | 显存 vu/vt | `48C  2.0/8.0G  40W` |
| HOST | mem% | `62%` MEMORY | swap% | `38C  fan760  3d5h` |
| DISK | usage% | `71%` USED | （无） | `R 12.5 W 3.2 MB/s` / `TEMP 41C` |
| NETWORK | down/会话峰值% | `8.4` DOWN MB/s | （无） | `UP 1.2` / `Ethernet` |
| TRAFFIC | down/(up+dn)% | `4.0G` DOWN TODAY | （无） | `UP 0.5G` |

不可用分类（`av` 位 0，多见单显 pin 到无独显的 GPU）→ 中心显 `no data`。速率类（Net/Traffic）无天然 0-100%，弧按会话峰值/比例缩放。

## 3. 轮询 / 配置 / 状态机

- **配置** `state::MonitorCfg{rotate, interval_s, single_cat, enabled_mask}`，web `set_monitor` 改 + NVS key `moncfg`（打包 uint32）持久化；`buildStateJson` 回 `monitor{rotate,interval,single,cats[],available[]}` 供 web 回填 + 按 `available` 置灰复选框。默认 **rotate=true / 20s / 全 6 类**。
- **状态机**（`tick`）：snapshot `seq` 变 → 重绘当前卡（数据刷新）；`rotate_` 且超 `interval` → `advanceCard()` 跳下一个「启用∧可用」卡。`onEnter` 按 cfg 选初始卡（单显=single，轮询=首个可用）；`applyState` 监控 cfg 变 → 重新同步 + 重置轮询 + 重绘（**本 mode 深底固定，不再跟随 g_state.bg_color**）。
- **底部圆点** = `enabled_mask & av` 的卡数；单显/单卡不画。

## 4. 轮询 task（常驻，懒建于首次 onEnter）

- prio 1，栈 8192B；`g_active`（onEnter/onExit 控）门控。
- 节奏：首帧前/离线 5s 重试，成功后 **5s**（显实时速率，PC 端 2s 采样）；onEnter 置 `g_poll_now` 立即拉一次。
- HTTP：`WiFiClient`+`HTTPClient`，连接/读 3s 超时，`GET http://<pc_ip>:8080/stats.json`，仅 200 解析。
- 并发：`Snapshot`（task 单写、loop tick 单读）经 `portMUX` 短临界区拷贝；`seq` 变才重绘（含上线↔离线翻转）。`pcmon::availableMask()` 供 `buildStateJson` 读 `g_snap.av`。

## 5. 内存 / 渲染
- 4bpp `M5Canvas` 240×240≈28KB，懒建一次、保留不释放（同 claude_status / face_show）；onEnter 打 `freeheap`（四 sprite 共存核对）。
- 仅 `seq` 变 / 轮询切卡时 `fillSprite(kBg)`→画→`pushSprite` 一次（无逐帧动画，无闪烁）。
- 占位态：未配 IP→「Set PC IP」；无 WiFi→「No WiFi」；首帧未拉到→「Connecting」。

## 6. 不改 / scope
- 不做历史曲线 / web 端镜像读数 / 插件（天气股票）/ 横竖屏 / 多皮肤。
- 编码器/触摸本地翻卡 = v0.3.0 Phase 17（本期仅 web 配置 + 自动轮询）。
- PC 服务 LHM 后端细节（pythonnet/netfx、管理员 + PawnIO、打包）见 `pc_monitor/README.md` + `pc_monitor/lib/README.md`。
