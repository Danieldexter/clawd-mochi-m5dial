# cc_hooks — Claude Code Status Light

中文：[README.md](./README.md)

Make the M5Dial mirror the state of your running Claude Code session with **Clawd's facial expressions**:

| Device shows | Meaning | Hook event that triggers it |
|---|---|---|
| Green / focused animation | **Thinking** (Claude is working) | `UserPromptSubmit` / `PostToolUse` → `s=working` |
| Amber `!` / alert animation | **Waiting for you** (confirmation / permission / a question) | `PermissionRequest` → `s=waiting` |
| Resting / idle animation | **Session ended / idle** | `Stop` → `s=idle` |

> Inspired by [DemoJj/claude-code-traffic-light](https://github.com/DemoJj/claude-code-traffic-light) (a macOS menu-bar traffic light); this project moves it onto a physical device and uses expressions instead of a small light.

> **Why `waiting` uses `PermissionRequest`, not `Notification`**: the current Claude Code `Notification` event has an `idle_prompt` subtype (fires after ~60s idle, and on every turn in some versions); mapping it wholesale to `waiting` makes the device show "waiting" when the session has actually ended / nothing is pending. `PermissionRequest` is a permission lifecycle event — it fires only when a permission dialog (or a tool that needs your call, like `AskUserQuestion`) actually appears, regardless of terminal focus, so it's a more accurate `waiting` signal. `PostToolUse → working` pulls the state back to "thinking" after you confirm / answer.

---

## How it works

In normal STA mode the device is an HTTP server on your LAN (`http://clawd-mochi.local`). It **can't know** what Claude Code is doing on your computer — and the only mechanism Claude Code has to emit "session state changed" to the outside world is **hooks**. So linking = you register a few hooks in Claude Code; whenever the state changes they run a one-line `curl` that pushes it to the device's `GET /cc?s=...` endpoint. On receipt the device:

- **not in canvas mode** → **auto-switches into link mode** and shows the matching expression;
- **in canvas mode** → does not interrupt your drawing (state is recorded silently);
- you can leave link mode anytime by picking another Mode in the web panel.

No Python, just `curl`.

---

## Prerequisites

1. Device provisioned and in normal (STA) mode, on the **same Wi-Fi** as your computer.
2. `curl` available (built in on Windows 10+ / macOS / mainstream Linux).
3. Claude Code installed and you can edit its `settings.json`.

---

## Step 1: Confirm the device address (`.local` or IP)

The hook commands default to `clawd-mochi.local`. First confirm this computer can resolve it — open **http://clawd-mochi.local** in a browser; if the control panel loads, mDNS works, use `.local`.

If it won't open (some Windows need Bonjour for `.local`), use the device **IP** instead:

- the `[prov] STA connected, IP=192.168.x.x` line in the serial log, or
- your router's device list.

**In every command below, replace `clawd-mochi.local` with that IP** (e.g. `http://192.168.1.23/cc?s=working`).

---

## Step 2: Verify the device side manually (no config yet)

Make sure the device is in a **non-canvas** expression mode, then run:

```bash
curl "http://clawd-mochi.local/cc?s=working"   # device enters link "thinking", returns ok
curl "http://clawd-mochi.local/cc?s=waiting"   # → "waiting"
curl "http://clawd-mochi.local/cc?s=idle"      # → "idle"
```

If all three change the device's face and return `ok`, the device side works — go to Step 3. If they hang / fail to resolve, go back to Step 1 and use the IP.

> On Windows PowerShell write **`curl.exe`** (bare `curl` is an alias for `Invoke-WebRequest` and won't accept these args).

Optional — verify the **project registry** (the `p=` param makes the device remember the project for the Settings dropdown):

```bash
curl -G "http://clawd-mochi.local/cc?s=working" --data-urlencode "p=/path/to/project-a"
curl -G "http://clawd-mochi.local/cc?s=working" --data-urlencode "p=/path/to/project-b"
```

Then open Settings; `project-a` / `project-b` should appear in the **Claude Project Scope** dropdown.

---

## Step 3: Merge the hooks into `settings.json`

Config file location:

- macOS / Linux: `~/.claude/settings.json`
- Windows: `%USERPROFILE%\.claude\settings.json` (i.e. `C:\Users\YourName\.claude\settings.json`)

**Merge the `hooks` key below in** — merge, **don't replace the whole file**: if the file already has other keys (`env` / `theme` / ...), add `hooks` as a new top-level key and remember to put a comma after the previous key. Install it **globally**; `$CLAUDE_PROJECT_DIR` auto-fills the current project path, so no per-project config is needed.

Copy **one** version depending on your shell.

### Version A — macOS / Linux / Windows (Git Bash)

On Windows, Claude Code **uses Git Bash to run hooks by default if Git is installed**, so this POSIX version fits most cases:

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

(Same as [`settings.example.json`](settings.example.json) in this folder — you can use that file directly.)

### Version B — Windows without Git Bash (hooks run in PowerShell)

Use this **only** if your Windows has no Git Bash and hooks fall back to PowerShell. Differences: `curl.exe` (not bare `curl`), `$env:CLAUDE_PROJECT_DIR`, `Out-Null; exit 0`:

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

### The four easiest things to get wrong

1. **Comma**: when adding `hooks` after an existing key, put a `,` after the previous key, or the JSON is invalid and Claude Code can't read your settings.
2. **Keep the escapes**: the `\"` in the commands are escaped quotes inside the JSON string — leave them as-is.
3. **You must discard stdout** (the `>/dev/null 2>&1` / `Out-Null` in the blocks): the stdout of the `UserPromptSubmit` / `PostToolUse` / `SessionStart` hooks is **injected into the conversation context / fed back to Claude**; without redirection curl's `ok` pollutes the context.
4. **Keep the trailing `|| true` (`; exit 0` on PowerShell)**: when the device is offline or `.local` resolves slowly, curl exits non-zero; without it Claude Code pops `Stop hook error: Failed with non-blocking status code` every turn (`UserPromptSubmit` / `Stop` fire every turn, so they surface first). State push is best-effort — an offline device must not raise errors. `--max-time 2` gives mDNS resolution headroom and keeps the hook from stalling Claude Code when the device is offline.

---

## Step 4: Restart the session + end-to-end check

After editing `settings.json`, **restart the Claude Code session** for hooks to take effect, then:

| Action | Device expected |
|---|---|
| Send a message | switches to "thinking" (working) |
| Claude asks for permission / asks you a question | "waiting" (amber `!`) |
| After you confirm / answer, Claude keeps working | back to "thinking" (working, driven by `PostToolUse`) |
| Session ends, then you sit idle | "idle" — **no more false waiting** |

---

## Project scope (Settings dropdown)

`-G --data-urlencode "p=$CLAUDE_PROJECT_DIR"` passes the current project dir as an identifier. The device stores recent projects (up to 8, newest-first) and shows them in the **Claude Project Scope** dropdown in Settings:

- `All projects` (default) → any project's session can drive the device;
- a specific project → only that project's sessions link; other background sessions won't grab the screen.

> A project must trigger a hook once (send a message in it, or `curl` manually with `p=`) before it appears in the dropdown.

---

## Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| Manual curl (Step 2) already fails | `.local` doesn't resolve → use the device IP (Step 1); or device isn't in STA mode / on a different Wi-Fi |
| Manual curl works, but sending a message does nothing | wrong shell variant → on Windows try **Version B** (PowerShell); also confirm you **restarted the session** |
| `Failed with non-blocking status code` every turn | curl exits non-zero and the command lacks the `\|\| true` guard → use the latest commands with `\|\| true` (`; exit 0` on PowerShell); update the ones already pasted in your `~/.claude/settings.json` too. **If linking is also dead**, the hook's shell (Git Bash) can't resolve `.local` → replace `clawd-mochi.local` with the device IP |
| State links fine, but the Settings dropdown stays empty | older firmware without the project registry → flash the latest firmware (`pio run -t upload` + `uploadfs`) |
| `ok` shows up in your prompt | missing the stdout redirect (`>/dev/null 2>&1`, or `Out-Null` on PowerShell) |

---

## Optional

- `PostToolUse → s=working` is configured by default; it returns the state to "thinking" after you confirm / answer. It **fires once per finished tool** (a little chatty but idempotent / flicker-free); if you often code while the device is offline and don't want every tool call to wait out `--max-time`, delete this hook (trade-off: after you confirm, the device stays on "waiting" until the turn ends).
- Auto-link is **always on**, no switch. To avoid it, switch to canvas mode (canvas is never interrupted).
- Don't want auto-follow, just light it up occasionally: skip hooks and run the Step 2 `curl` directly, or make a `working.bat` / `idle.bat` to double-click.
