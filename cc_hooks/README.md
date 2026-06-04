# cc_hooks — Claude Code 联动状态灯

English: [README.en.md](./README.en.md)

让 M5Dial 用 **Clawd 的表情**实时反映你正在跑的 Claude Code 会话状态：

| 设备表现 | 含义 | 触发它的 hook 事件 |
|---|---|---|
| 绿色 / 专注动效 | **思考中**（Claude 在干活） | `UserPromptSubmit` / `PostToolUse` → `s=working` |
| 琥珀 `!` / 提示动效 | **待确认 / 待权限 / 问你问题** | `PermissionRequest` → `s=waiting` |
| 待命 / 休息动效 | **会话结束 / 待命** | `Stop` → `s=idle` |

> 灵感来自 [DemoJj/claude-code-traffic-light](https://github.com/DemoJj/claude-code-traffic-light)（macOS 菜单栏交通灯），本项目把它搬到实体设备上、用表情代替小灯。

> **为什么 `waiting` 用 `PermissionRequest` 而不是 `Notification`**：当前版 Claude Code 的 `Notification` 含 `idle_prompt` 子类型（空闲约 60s 也触发，部分版本每轮回答后都触发），整体映射成 `waiting` 会导致"会话已结束 / 没事却显示待确认"。`PermissionRequest` 是权限生命周期事件，只在真弹权限框（或 `AskUserQuestion` 等需你拍板的工具）时触发、与终端是否聚焦无关，作为 `waiting` 信号更准。`PostToolUse → working` 负责在你确认 / 回答后把状态拉回思考态。

---

## 工作原理

设备在**正常 STA 模式**下是局域网里的一个 HTTP 服务器（`http://clawd-mochi.local`）。它**无法主动知道**你电脑上 Claude Code 在干嘛——而 Claude Code 唯一把"会话状态变化"吐到外部的机制就是 **hooks**。所以联动 = 在 Claude Code 里挂几个 hook，状态一变就跑一行 `curl` 把它推给设备的 `GET /cc?s=...` 端点。设备收到后：

- 当前**不在 canvas 模式** → **自动切入联动模式**并显示对应表情；
- 当前**在 canvas 模式** → 不打断你画画（状态静默记录）；
- 任何时候都可以在 Web 面板点别的 Mode 手动退出联动。

零 Python 依赖，纯 `curl`。

---

## 前置条件

1. 设备已配网并处于正常运行（STA）模式，和电脑在**同一个 WiFi**。
2. 系统有 `curl`（Windows 10+ / macOS / 主流 Linux 自带）。
3. 装过 Claude Code 且能改它的 `settings.json`。

---

## 第 1 步：确认设备地址（`.local` 还是 IP）

hook 命令默认用 `clawd-mochi.local`。先确认你这台电脑能解析它——浏览器打开 **http://clawd-mochi.local** ，能看到控制面板就说明 mDNS 正常，直接用 `.local`。

打不开（部分 Windows 需要装 Bonjour 才支持 `.local`）时，改用设备 **IP**：

- 串口 log 里的 `[prov] STA connected, IP=192.168.x.x`，或
- 路由器后台的设备列表。

**后续所有命令里，把 `clawd-mochi.local` 换成这个 IP 即可**（如 `http://192.168.1.23/cc?s=working`）。

---

## 第 2 步：先手动验证设备端（不碰任何配置）

确认设备处于**非 canvas** 的表情模式，然后逐条跑：

```bash
curl "http://clawd-mochi.local/cc?s=working"   # 设备切入联动「思考」，返回 ok
curl "http://clawd-mochi.local/cc?s=waiting"   # → 「待确认」
curl "http://clawd-mochi.local/cc?s=idle"      # → 「待命」
```

三条都让设备变脸、且返回 `ok` → 设备端通了，进第 3 步。若卡住 / 解析失败 → 回第 1 步用 IP。

> Windows PowerShell 里请写 **`curl.exe`**（裸 `curl` 是 `Invoke-WebRequest` 的别名，吃不下这些参数）。

可选：验证**项目注册表**（带 `p=` 参数，设备会把项目记进 Settings 的下拉）：

```bash
curl -G "http://clawd-mochi.local/cc?s=working" --data-urlencode "p=/path/to/project-a"
curl -G "http://clawd-mochi.local/cc?s=working" --data-urlencode "p=/path/to/project-b"
```

之后打开 Settings，`project-a` / `project-b` 应出现在 **Claude Project Scope** 下拉里。

---

## 第 3 步：把 hooks 合并进 `settings.json`

配置文件位置：

- macOS / Linux：`~/.claude/settings.json`
- Windows：`%USERPROFILE%\.claude\settings.json`（即 `C:\Users\你的用户名\.claude\settings.json`）

**把下面的 `hooks` 键合并进去**——是合并、**不是替换整个文件**：如果文件已有别的键（`env` / `theme` 等），把 `hooks` 作为新的顶层键加进去，并注意前一个键末尾要补逗号。装到**全局**配置即可，命令里的 `$CLAUDE_PROJECT_DIR` 会自动带上当前项目路径，无需每个项目单独配。

按你的 shell 选**其中一个**版本复制。

### 版本 A — macOS / Linux / Windows（Git Bash）

Claude Code 在 Windows 上**装了 Git 就默认用 Git Bash 跑 hook**，所以大多数情况用这个 POSIX 版本：

```json
{
  "hooks": {
    "UserPromptSubmit": [
      { "hooks": [ { "type": "command", "command": "curl -s --max-time 2 -G \"http://clawd-mochi.local/cc?s=working\" --data-urlencode \"p=$CLAUDE_PROJECT_DIR\" >/dev/null 2>&1 || true" } ] }
    ],
    "PostToolUse": [
      { "hooks": [ { "type": "command", "command": "curl -s --max-time 2 -G \"http://clawd-mochi.local/cc?s=working\" --data-urlencode \"p=$CLAUDE_PROJECT_DIR\" >/dev/null 2>&1 || true" } ] }
    ],
    "PermissionRequest": [
      { "hooks": [ { "type": "command", "command": "curl -s --max-time 2 -G \"http://clawd-mochi.local/cc?s=waiting\" --data-urlencode \"p=$CLAUDE_PROJECT_DIR\" >/dev/null 2>&1 || true" } ] }
    ],
    "Stop": [
      { "hooks": [ { "type": "command", "command": "curl -s --max-time 2 -G \"http://clawd-mochi.local/cc?s=idle\" --data-urlencode \"p=$CLAUDE_PROJECT_DIR\" >/dev/null 2>&1 || true" } ] }
    ],
    "SessionStart": [
      { "hooks": [ { "type": "command", "command": "curl -s --max-time 2 -G \"http://clawd-mochi.local/cc?s=idle\" --data-urlencode \"p=$CLAUDE_PROJECT_DIR\" >/dev/null 2>&1 || true" } ] }
    ]
  }
}
```

（和同目录的 [`settings.example.json`](settings.example.json) 一致，可直接拿那个文件参考。）

### 版本 B — Windows 且没装 Git Bash（hook 走 PowerShell）

仅当你 Windows 上没有 Git Bash、hook 回退到 PowerShell 时用这个。区别：`curl.exe`（非裸 `curl`）、`$env:CLAUDE_PROJECT_DIR`、`| Out-Null; exit 0`：

```json
{
  "hooks": {
    "UserPromptSubmit": [
      { "hooks": [ { "type": "command", "command": "curl.exe -s --max-time 2 -G \"http://clawd-mochi.local/cc?s=working\" --data-urlencode \"p=$env:CLAUDE_PROJECT_DIR\" | Out-Null; exit 0" } ] }
    ],
    "PostToolUse": [
      { "hooks": [ { "type": "command", "command": "curl.exe -s --max-time 2 -G \"http://clawd-mochi.local/cc?s=working\" --data-urlencode \"p=$env:CLAUDE_PROJECT_DIR\" | Out-Null; exit 0" } ] }
    ],
    "PermissionRequest": [
      { "hooks": [ { "type": "command", "command": "curl.exe -s --max-time 2 -G \"http://clawd-mochi.local/cc?s=waiting\" --data-urlencode \"p=$env:CLAUDE_PROJECT_DIR\" | Out-Null; exit 0" } ] }
    ],
    "Stop": [
      { "hooks": [ { "type": "command", "command": "curl.exe -s --max-time 2 -G \"http://clawd-mochi.local/cc?s=idle\" --data-urlencode \"p=$env:CLAUDE_PROJECT_DIR\" | Out-Null; exit 0" } ] }
    ],
    "SessionStart": [
      { "hooks": [ { "type": "command", "command": "curl.exe -s --max-time 2 -G \"http://clawd-mochi.local/cc?s=idle\" --data-urlencode \"p=$env:CLAUDE_PROJECT_DIR\" | Out-Null; exit 0" } ] }
    ]
  }
}
```

### 四个最容易翻车的点

1. **逗号**：把 `hooks` 加在已有键后面时，前一个键末尾要补 `,`，否则 JSON 非法、Claude Code 读不了设置。
2. **转义别删**：命令里的 `\"` 是 JSON 字符串内的转义引号，原样保留。
3. **必须丢弃标准输出**（块里的 `>/dev/null 2>&1` / `| Out-Null`）：`UserPromptSubmit` / `PostToolUse` / `SessionStart` 等 hook 的 stdout 会被**注入进对话上下文 / 回灌给 Claude**，不重定向的话 curl 的 `ok` 会污染上下文。
4. **结尾的 `|| true`（PowerShell 版 `; exit 0`）别删**：设备离线 / `.local` 解析慢时 curl 会以非零码退出，没有它 Claude Code 每轮会弹 `Stop hook error: Failed with non-blocking status code`（`UserPromptSubmit` / `Stop` 每轮必触发，最先暴露）。状态推送是「锦上添花」，设备不在线也不该报错；`--max-time 2` 给 mDNS 解析留余量、设备离线时也不会久卡。

---

## 第 4 步：重启会话 + 端到端验证

改完 `settings.json` 后**重启 Claude Code 会话**使 hook 生效，然后：

| 动作 | 设备预期 |
|---|---|
| 发一条消息 | 秒切「思考」(working) |
| Claude 弹权限确认 / 问你问题 | 「待确认」(waiting，琥珀 `!`) |
| 确认 / 回答后 Claude 继续干活 | 回到「思考」(working，由 `PostToolUse` 驱动) |
| 会话结束、之后干等不动 | 「待命」(idle)，**不再误报 waiting** |

---

## 项目作用域（Settings 下拉）

`-G --data-urlencode "p=$CLAUDE_PROJECT_DIR"` 把当前项目目录作标识传给设备。设备把最近收到的项目写进注册表（最多 8 个，newest-first），显示在 Settings 的 **Claude Project Scope** 下拉里：

- 选 `All projects`（默认）→ 任何项目的会话都能驱动设备；
- 选定某个项目 → 只有该项目的会话联动，其他后台会话不抢屏幕。

> 某个项目第一次需要先触发一次 hook（在该项目里发条消息，或手动 curl 带 `p=`）才会出现在下拉里。

---

## 排错

| 现象 | 多半原因 / 解法 |
|---|---|
| 手动 curl（第 2 步）就不通 | `.local` 解析不了 → 用设备 IP（第 1 步）；或设备不在 STA 模式 / 不同 WiFi |
| 手动 curl 通，但发消息设备不动 | hook 的 shell 变体选错 → Windows 试**版本 B**（PowerShell）；并确认改完**重启了会话** |
| 每轮弹 `Failed with non-blocking status code` | curl 非零退出、命令缺 `\|\| true` 兜底 → 用最新带 `\|\| true`（PowerShell `; exit 0`）的命令；你 `~/.claude/settings.json` 里已粘贴的也要同步。**若联动也不动**，则是 hook 的 shell（Git Bash）解析不了 `.local` → 把命令里 `clawd-mochi.local` 换成设备 IP |
| 状态联动正常，但 Settings 下拉一直空 | 设备固件较旧、不含项目注册表 → 烧录最新固件（`pio run -t upload` + `uploadfs`） |
| prompt 里冒出 `ok` 字样 | 漏了输出重定向（`>/dev/null 2>&1`，或 PowerShell 的 `Out-Null`） |

---

## 可选

- `PostToolUse → s=working` 已默认配置，作用是"确认 / 回答后回到思考态"。它**每个工具跑完都推一次**（略吵但幂等无闪）；若你常在设备离线时编码、不想每次工具调用多等 `--max-time`，可删掉这条（代价：点确认后会一直停在「待确认」直到本轮结束）。
- 默认**常开自动联动**，无开关。不想被联动时切到 canvas 模式即可（canvas 不被打断）。
- 不想自动跟随、只想偶尔手动点亮：跳过 hook，直接跑第 2 步的 `curl`，或做成桌面 `working.bat` / `idle.bat` 双击运行。
