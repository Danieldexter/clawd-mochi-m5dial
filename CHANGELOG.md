# Changelog

本项目所有重要变更记录于此。
格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循 [Semantic Versioning](https://semver.org/lang/zh-CN/)。

## [Unreleased]

（v0.2.0 累积区）

### Added

- **PC Monitor 模式（Phase 14 / 14b）**：`src/modes/pc_monitor.{h,cpp}`（`ModeId::PC_MONITOR`）—— **6 大分类圆屏卡片**（CPU / 磁盘 / 显卡 / 主机 / 网络 / 流量），**默认轮询切换**（20s，web 可配单显/轮询/间隔/启用分类），**深色基底 `#1c1c20` + 每类强调色**（CPU 橙红 / Disk 金 / GPU 紫 / Host 蓝 / Net 绿 / Traffic 青）+ **弧形仪表 / 进度条 / 数值混排** + 底部圆点指示当前卡。每卡：rim 弧环显主指标%（CPU/GPU/Host/Disk 负载、Net/Traffic 按会话峰值/比例缩放）、中心大字 + 标签、CPU/GPU/Host 带次级进度条（温度/显存/swap）、读数行显频率/温度/功耗/速率等；不可用分类（无独显等）轮询自动跳过、单显时显 `no data`。出站 HTTP 走**常驻 FreeRTOS task**（prio1 栈8K，§1.6）：首帧前/离线 5s 重试，成功后 **5s** 拉一次（显实时速率），3s 超时；`Snapshot`（6 子结构 + `NAN`/-1 哨兵表缺省）经 `portMUX` 短临界区 task↔loop 传递，`seq` 变才重绘。渲染 **4bpp 离屏 `M5Canvas`**（240×240≈28KB，懒建保留，仿 face_show，accent 槽逐卡 `setPal565` 重涂）+ 仅 `seq` 变 / 轮询切卡时 `fillSprite`→画→`pushSprite` 一次（无闪烁，**不增内存**）。时钟取自 PC 无需 NTP；温度一律 `%dC`（避 ° 字形缺失）。配置 `state::MonitorCfg{rotate,interval_s,single_cat,enabled_mask}` 经 WS `set_monitor` 改 + NVS key `moncfg`（打包 uint32）持久化；`buildStateJson` 回 `monitor{rotate,interval,single,cats[],available[]}`，`data/index.html`/`app.js`/`style.css` 加 `.pcmon-only` 配置面板（Single/Rotate + 间隔 + 6 分类复选，按 `available` 置灰）。**本 mode 深底固定，不再跟随 `g_state.bg_color`**。`pcmon::availableMask()` 供 state 广播读 `av`。**stats.json 升级为嵌套 v2 契约**（`cpu/gpu/host/disk/net/traf` 子对象 + `av` 可用性 map + `clk`，短键、`null` 缺省），设备 `jnum()=(v|NAN)` 解析。无新固件依赖（HTTPClient 随 core）。编译 RAM 16.7%(54844B) / Flash 50.7%(1694829B)。详见 `docs/phase14_pc_monitor_spec.md`
- **PC 服务 LibreHardwareMonitor 后端（Phase 14b，`pc_monitor/pc_monitor.py` 重写）**：数据两层 —— **psutil**（跨平台、零特权）给 CPU 占用/频率/核心、内存/swap、磁盘占用+读写速率、网络上下行+日流量（psutil 计数差分 + 午夜基线持久化 `traffic.json`）、uptime、时钟；**LibreHardwareMonitor**（Windows 可选，pythonnet/netfx 加载 `lib/*.dll`）补温度/风扇/GPU/功耗（psutil 在 Win 上拿不到）。**常驻采样线程**每 `refresh_interval`（默认 2s）`Computer.Accept(UpdateVisitor)` 刷新 + 读传感器（含 Motherboard 的 SubHardware 板温/风扇）→ 组装嵌套 dict 入锁缓存，`/stats.json` 只回缓存（修掉旧版 handler 里 `cpu_percent(0.1)` 的 100ms 阻塞）。**优雅降级**：非 Windows / 无 DLL / 非 64 位 → 纯 psutil，缺字段 `null`、`av` 标可用性。`requirements.txt` 加 `pythonnet`（仅 Win）；新增 `pc_monitor/lib/README.md`（DLL 放置 + 管理员 + `winget install PawnIO` 解锁温度/风扇 + x64 说明）；`README.md` 重写后端/打包/API/配置章节；`.gitignore` 加 `pc_monitor/lib/*.dll` + `traffic.json`
- **Reminder 定时提醒（Phase 13）**：新增 `src/services/reminder.{h,cpp}`（≤5 条 NVS 持久化）+ `src/modes/reminder_overlay.{h,cpp}`（瞬态全屏消息 mode，`ModeId::REMINDER_OVERLAY`）。时间模型 = **每日定时 HH:MM**（daily 重复 / 一次性可选），经 **NTP** 取墙钟（`main.cpp` STA 连上 `configTime`，时区固定 UTC+8）；`reminder::tick`（loop 限频 1s）用 `getLocalTime` 比较，未同步不触发，按 `yday*1440+min` 键防同分钟重复，一次性触发后删除写回 NVS。到点 loop `consumeFired` → `show()`+`setMode(REMINDER_OVERLAY)` 切瞬态全屏（`efontCN_24` 中英换行消息 + 顶部琥珀硬方块 `!` + 底部倒计时条），30s 或 `BtnA.wasClicked()` 轻点后回原 mode。**关键技巧**：overlay 永不写 `g_state.current_mode`（`buildStateJson` 的 mode 取自它而非 `currentId()`）→ web 仍显底层 mode、无需 `kModeMappings` 项 / 无 web 按钮；可打断任何 mode 含 canvas（sprite 保留不丢画作），cc 自动切入加 `!= REMINDER_OVERLAY` 守卫不抢占闹钟。WS 加 `reminder_add`/`reminder_del`，state 广播 `reminders[]`；`data/index.html` 主面板加 Reminders section（time+msg+daily+Add+列表）+ `app.js` `renderReminders`。NVS 复用 namespace `clawd` key `reminders`（`H|M|daily|msg` 行序列化，仿 cc_projects）。`config.h` 加 `kRemindersMax`/`kReminderMsgLen`/`kReminderShowMs`/`kTzOffsetSec`/`kNtpServer1,2`。`efontCN_24` CJK 字库引入约 +560KB flash（占用升至 50.6%，余量充足）。详见 `docs/phase13_reminder_spec.md`
- **Face System（Phase 12）**：不照搬 mumuer1024 自动生成的 6×6 `faces_code.h`；仅保留 `face_wuyu` / `anim_smile` 等 7 静 + 10 动命令语义，M5Dial 版重新设计为圆屏友好的矢量/块混合表情。新增 `FaceSpec` 注册表（key / label / 动画帧数 / 循环次数 / 结束保持策略），`face_show` 继续用 4bpp 调色板离屏 `M5Canvas`（240×240≈28KB）+ 每帧一次 `pushSprite`；动画由 `tick()` 非阻塞推进，无 `delay()`。WS 协议升级为 `{type:"set_face", key:"face_wuyu"}`，state 广播 `face_key`，Web 下拉显示真实表情名，不再暴露 `Face 0..16`；删除 `CcStyle::FACES` 占位，Face System 保持独立 mode。新增 `docs/phase12_face_system_spec.md`。17 表情统一到招牌**黑硬边方眼**语言（`eyeRect` + 眼睑遮罩 + 白 glint，与 eyes_normal / claude_status CLAWD 眼同源），全程**无嘴 / 无星芒 / 无腮红**（情绪仅靠眼形 + 单个朴素点缀 `! ? z ♥ ✓ ╳`，星芒留给联动模式专用）；对比修正：`face_X` / `anim_dead` 的 X 由红改黑（红在橙/红底均不可见），问号由 cyan 回归 amber 点缀色系；删 `socketEye` / `openEye` / 所有嘴 primitive / 腮红。**6 张脸（感叹 / 问号 / 对 / 墨镜 / 心跳 / 睡觉）回归硬边眼语言**：`drawBang`（竖杆+点）/ `drawQuestion`（顶横+右竖钩+回折+居中竖+点）/ `drawZ`（横杆+块斜+横杆）由 `capsule`（圆角胶囊）/ `thickLine` 软质改写为**全 `fillRect` block**（移植 `claude_status::drawBang`/`drawZ`），消除「圆角符号」与招牌全硬边语言的冲突；感叹 ≈ waiting、问号 ≈ waiting 换符号（均**去圆环**），睡觉**复刻 CLAWD idle 打盹**半阖眼 + 右上飘 cream block `z`；对 / 心跳的喜悦弯眼加深、`drawCheck` / `drawHeart` 比例调利落，墨镜镜片对齐招牌眼位；软质 primitive 全部退役后删孤儿 `capsule()`
- **cc_hooks 项目注册表（Phase 11b / Commit 2）**：`/cc?s=...&p=$CLAUDE_PROJECT_DIR` 消费时先把项目写入 NVS `ccprojects`（最多 8 个，newest-first），再按 `cc_scope` 过滤；state JSON 增加 `cc_projects` 数组。Settings 页新增 **Claude Project Scope** 下拉（All projects + 最近项目，显示 basename、value 保留完整路径）；`set_cc_scope` 继续持久化选中项目，非匹配项目不再抢占联动屏幕
- **Claude Code 联动状态模式（Phase 11，本项目原创招牌特性）**：设备用表情/动效实时反映 Claude Code 会话状态（思考中 / 待确认 / 待命）。提供**两种可选视觉风格**（Settings 页切换 + NVS 记忆）：**CLAWD 情绪眼**（idle 竖向呼吸 + 随机眨眼 + 闲置 12s 打盹飘 `z` / working 下垂专注眼 + 头顶绿色 Claude 星芒脉冲 / waiting 瞪大眼带高光 + 弹跳琥珀 `!` + 扫过的亮弧 crest）与 **SPARKLE 星芒核心**（working 大星芒慢转 + 逐射线错相微颤 / waiting 冻结转琥珀 + 心跳放大 + `!` / idle 缩成小静星 + 下方回归慢眨眼）。新增 `GET /cc?s={working|waiting|idle}` HTTP 端点收 hook 推送；非 canvas 表情模式对话时**自动切入**（canvas 不打断绘画）；`/cc` 切模式延迟到主循环执行避开 async 绘屏竞争。渲染走 **4bpp 调色板离屏后备缓冲**（`M5Canvas` 240×240≈28KB）：擦旧 + 画新都在离屏合成，每帧仅一次 `pushSprite` 推屏，面板永不显示中间擦除态 → 无闪烁；全部动画极值守 §6 安全圈 r≤110。新增 `src/modes/claude_status.{h,cpp}` + `cc_hooks/`（curl hooks 示例 + 说明，零 Python）。受 [DemoJj/claude-code-traffic-light](https://github.com/DemoJj/claude-code-traffic-light) 启发
- **WiFi 配网（Phase 9）**：**按需**（BtnA 长按 5s / Settings Reset WiFi）建开放配网 AP `Clawd-Mochi-Setup` + DNS captive portal（手机自动弹配网页）；配网屏显示 WiFi 加入二维码（扫码即入网）；setup.html 收 SSID/密码/PC IP → 存 NVS（namespace `clawd`）→ 重启进 STA
- **STA 正常运行模式**：设备加入用户 WiFi（15s 超时→重试一次→回退配网），经 mDNS `clawd-mochi.local` 访问；取代 v0.1.0 常驻控制 AP
- **Settings 页（Phase 10）**：settings.html WS 原生改 PC IP / 重启 / Reset WiFi；`state` 广播加 `pc_ip` + `ssid` 字段
- **BtnA 长按 5s 进配网**：置 NVS 一次性标志 + 重启进配网二维码屏（**非破坏性**，保留已存凭据；替代原项目 GPIO 5，M5Dial 无此引脚）
- WS 协议加 `set_pc_ip` / `wifi_reset` / `restart` / `set_cc_style` 四个 message type
- `src/services/provisioning.{h,cpp}`：NVS 凭据 + WiFi 生命周期 + captive portal + mDNS + 延迟重启（避免在 async 回调里 `ESP.restart()`）的单一拥有者

### Changed

- **Web 控制面板重构（Phase 15）**：`data/index.html` / `app.js` / `style.css` 从扁平长列表（不相关 mode 控件仅 `.disabled` 弱隐藏 `opacity:.35` 仍占位 → 一屏全是灰控件）重构为 **mode 入口 chip 网格（emoji + 标签）+ 每 mode 内联 `.panel` 容器**（点 chip = 切设备 + 只显该 mode 容器、其余 `display:none`；`data-panel` 空格分隔多 mode → 两眼睛 mode 共用 Speed 容器；点击乐观切换免等 WS 回包）+ **全局常驻区**（背光 / BG 取色器 / Reminders 折叠 `<details>`）。**PC Monitor 容器新增网页实时镜像**：网页直接 `fetch http://<pc_ip>:8080/stats.json`（服务已开 CORS、页面与目标同 http，纯客户端、**零固件改**），按 `av` 渲染 6 大分类卡（与设备 `pc_monitor.cpp kAccent565` 同源的每类强调色 + 进度条 cpu/gpu 负载·host 内存·disk 占用 + 温度/频率/功耗/速率读数，缺值 `--`；离线显 `PC offline` 保留上次卡片不刷错，未配 IP 提示 `Set PC IP in Settings`），3s 轮询仅在该容器激活时跑（`AbortController` 2.5s 超时）。Claude Code 联动徽标由文字改为**状态 pill**（working 绿 / waiting 琥珀 / idle 灰）+ 作用域显示（cc_scope 空 = All projects + 项目数，否则 basename）。删 `.canvas-only/.claude-only/.faces-only/.pcmon-only` 弱隐藏机制（`setCanvasOnly` 等四函数 → 单一 `showPanel`），`.controls h3` 选择器改全局 `h3`（`.controls` 容器移除，settings.html 的 h3 仍在 `.controls` 内、渲染不变）。pad 越界守卫由 `.disabled` 类改为 `!padEl.offsetParent`（容器隐藏天然不响应）。**仅改 `data/`，不动固件 / WS 协议 / state / settings.html**——所需字段 `mode/speed/bg_color/pen_color/backlight/cc_status/cc_scope/cc_projects/face_key/reminders/monitor/pc_ip` 现有 `buildStateJson` 已全广播。
- **设备身份底色：红 → 橙红 `#ff4000`**：默认背景由 `#DA1100`（0xD880）改为 `#ff4000`（0xFA00，鲜亮橙红，作为设备整体身份色影响所有 mode）。真值源 `state.h`；`claude_code` 终端「底色==默认→强制黑底」哨兵 2 处（onEnter / applyState）同步改（否则切到 Claude Code 终端会误用橙底）；`eyes_normal` / `eyes_squish` / `claude_status` / `face_show` 4 个 mode 成员默认色 + `index.html` 取色器初值同步
- **默认开机进设备界面**：开机直接进设备动画（单机可玩），配网二维码改为**按需进入**（BtnA 长按 / Settings Reset WiFi）；不再「首启/无凭据强制配网」。连接失败也只单机运行不自动弹配网。`provisioning` 加 `requestSetupMode()`/`consumeSetupRequest()`（NVS 一次性标志 `setup`，启动读后即清）
- 网络模型反转：v0.1.0「永远是 AP」→ v0.2.0「正常 STA-only / 配网 AP_STA」，二者经重启切换、永不在有动画时共存（CLAUDE.md §9 同步更新）
- `config.h` 删 `kApSsid`/`kApPassword`，增 `kSetupApSsid`/`kMdnsHost`/`kNvsNamespace`/`kStaConnectTimeoutMs`
- `WebStack::begin()` → `begin(bool setup_mode)`，WiFi 改由 provisioning 在调用前拉起
- 依赖零新增：`DNSServer` / `ESPmDNS` / `Preferences` 均随 ESP32 Arduino core 自带

### Fixed

- **联动状态模式黑边闪烁（硬件实测反馈）**：根因为直绘屏下「整块擦背景 → 再画」每帧重复，肉眼可见中间擦除态（尤以星芒旋转 / 呼吸眼频繁重绘 / 打盹 `z` 移动为甚）。`claude_status` 改为渲染到 **4bpp 调色板离屏 `M5Canvas`**（240×240≈28KB；本机无 PSRAM，故用调色板省内存以与 Canvas 的 115KB sprite 共存，且保 RGB565 精确显色）再一次性 `pushSprite` 推屏——擦除态只发生在离屏缓冲、面板只收合成成品，闪烁消除。几何 / 时序 / 配色与风险清单全部不变；仅 `drawZ` 斜线由 AA 的 `drawWideLine` 换成非 AA `drawLine`（调色板 sprite 不支持 AA 混色）
- **SPARKLE waiting "!" 半刷新闪烁（硬件实测反馈）**：根因为心跳星芒的圆形擦盘（半径达 88px，顶部触及 y=32）每次重绘都吞掉居中 `!`（y∈[14,68]）的下半截，而 `!` 仅在弹跳位置 `topY` 变化时才重绘——心跳放大期间星芒逐帧重绘但 `topY` 在弹跳峰/谷静止数帧，`!` 下半遂残缺到下次 `topY` 变化才补回。修复：`tickSparkleWaiting` 增 `sparkle_redrawn` 标志，星芒一旦重绘即把 `!` 叠回（位置未变时免擦直接重绘）

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
