#!/usr/bin/env python3
"""
Clawd Mochi PC Monitor (v2)
系统监控数据 HTTP API + 系统托盘。

数据后端两层：
  - psutil（跨平台、零特权）：CPU 占用/频率/核心、内存/swap、磁盘占用+读写速率、网络上下行+日流量、uptime、时钟。
  - LibreHardwareMonitor（Windows，可选）：温度 / 风扇 / GPU / 功耗 —— psutil 在 Windows 上拿不到这些。
缺/不可用的字段一律输出 null，设备端显示 "--"。stats.json 为嵌套 v2 契约（见 docs/phase14_pc_monitor_spec.md）。

后台采样线程每 refresh_interval 秒刷新一次缓存；/stats.json 只回缓存（不在请求里做重活）。
"""

import os
import sys
import json
import time
import socket
import ctypes
import threading
import platform
from datetime import datetime

import psutil
from flask import Flask, jsonify
from flask_cors import CORS

# 系统托盘相关
try:
    import pystray
    from PIL import Image
    TRAY_AVAILABLE = True
except ImportError:
    TRAY_AVAILABLE = False
    print("Warning: pystray or Pillow not installed, tray icon disabled")

# ── 路径 / 配置 ───────────────────────────────────────────────

BASE_DIR = os.path.dirname(os.path.abspath(__file__))

def _resource_dir():
    # PyInstaller 冻结时把捆绑的 DLL 解包到 _MEIPASS；脚本运行时就是 BASE_DIR
    return getattr(sys, '_MEIPASS', BASE_DIR)

LIB_DIR      = os.path.join(_resource_dir(), 'lib')   # 放 LibreHardwareMonitorLib.dll (+ HidSharp.dll)
CONFIG_FILE  = os.path.join(BASE_DIR, 'config.json')
ICON_DIR     = os.path.join(BASE_DIR, 'icon')
TRAFFIC_FILE = os.path.join(BASE_DIR, 'traffic.json')  # 日流量基线持久化

DEFAULT_CONFIG = {
    "port": 8080,
    "refresh_interval": 2,   # 采样线程节奏（秒）
    "system_drive": None,    # 磁盘占用盘符；None=自动（Win C:\ / 其它 /）
}

def load_config():
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, 'r', encoding='utf-8') as f:
                cfg = json.load(f)
                return {**DEFAULT_CONFIG, **cfg}
        except Exception as e:
            print(f"Config load error: {e}")
    return dict(DEFAULT_CONFIG)

config = load_config()
PORT = config.get('port', 8080)

# ── Flask ─────────────────────────────────────────────────────

app = Flask(__name__)
CORS(app)

# ── LibreHardwareMonitor 后端（Windows 可选；失败则纯 psutil）─────────────

LHM_AVAILABLE = False
_computer = None
_visitor = None
_SensorType = None

def _is_admin():
    try:
        if platform.system() == 'Windows':
            return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:
        pass
    return False

ADMIN = _is_admin()

def init_lhm():
    """尝试加载 LibreHardwareMonitorLib.dll（netfx 路线）。任何失败都安静降级到纯 psutil。"""
    global LHM_AVAILABLE, _computer, _visitor, _SensorType
    if platform.system() != 'Windows':
        return
    dll = os.path.join(LIB_DIR, 'LibreHardwareMonitorLib.dll')
    if not os.path.exists(dll):
        print(f"[lhm] {dll} 不存在 → 纯 psutil 模式（温度/风扇/GPU 将为 null）")
        return
    try:
        # 运行时与位数须在 import clr 前定好；用当前 64 位解释器 + .NET Framework 宿主
        os.environ.setdefault('PYTHONNET_PYDLL',
                              f'python{sys.version_info.major}{sys.version_info.minor}.dll')
        from pythonnet import load
        load('netfx')
        import clr
        clr.AddReference(dll)
        from LibreHardwareMonitor.Hardware import Computer, SensorType, IVisitor

        class UpdateVisitor(IVisitor):
            __namespace__ = "ClawdMochiMonitor"  # pythonnet 要求 IVisitor 实现命名空间唯一
            def VisitComputer(self, computer): computer.Traverse(self)
            def VisitHardware(self, hardware):
                hardware.Update()
                for sub in hardware.SubHardware:  # 板温/机箱风扇在 SubHardware(SuperIO) 下，须各自 Update
                    sub.Update()
            def VisitSensor(self, sensor): pass
            def VisitParameter(self, parameter): pass

        c = Computer()
        c.IsCpuEnabled = True
        c.IsGpuEnabled = True
        c.IsMemoryEnabled = True
        c.IsMotherboardEnabled = True
        c.IsStorageEnabled = True
        c.IsNetworkEnabled = True
        c.Open()

        _computer = c
        _visitor = UpdateVisitor()
        _SensorType = SensorType
        LHM_AVAILABLE = True
        print(f"[lhm] LibreHardwareMonitor 就绪 (admin={ADMIN})。"
              f"{'' if ADMIN else ' 非管理员：CPU/主板温度与部分风扇可能为 null。'}")
    except Exception as e:
        print(f"[lhm] 初始化失败 → 纯 psutil：{e}")

def _sensor_value(s):
    v = s.Value
    return None if v is None else float(v)

def read_lhm():
    """读 LHM 传感器 → 各类 partial dict（仅温度/风扇/GPU/功耗/频率这些 psutil 给不了的）。失败返回空。"""
    out = {'cpu': {}, 'gpu': {}, 'host': {}, 'disk': {}}
    if not LHM_AVAILABLE:
        return out
    try:
        _computer.Accept(_visitor)
    except Exception as e:
        print(f"[lhm] update 失败：{e}")
        return out
    St = _SensorType
    cpu_fq_max = None
    try:
        for hw in _computer.Hardware:
            ht = hw.HardwareType.ToString()
            sensors = list(hw.Sensors)
            for sub in hw.SubHardware:
                sensors += list(sub.Sensors)
            for s in sensors:
                name = s.Name or ''
                st = s.SensorType
                val = _sensor_value(s)
                if val is None:
                    continue
                if ht == 'Cpu':
                    if st == St.Temperature and ('Package' in name or 'Tctl' in name or 'Tdie' in name):
                        out['cpu'].setdefault('tp', val)
                    elif st == St.Clock and 'Core' in name:
                        cpu_fq_max = val if cpu_fq_max is None else max(cpu_fq_max, val)
                    elif st == St.Power and 'Package' in name:
                        out['cpu']['pw'] = val
                    elif st == St.Fan and 'CPU' in name:
                        out['cpu'].setdefault('fan', val)
                elif ht in ('GpuNvidia', 'GpuAmd', 'GpuIntel'):
                    if st == St.Load and 'Core' in name:
                        out['gpu']['ld'] = val
                    elif st == St.Temperature and 'Core' in name:
                        out['gpu'].setdefault('tp', val)
                    elif st == St.SmallData and 'Memory Used' in name:
                        out['gpu']['vu'] = val
                    elif st == St.SmallData and 'Memory Total' in name:
                        out['gpu']['vt'] = val
                    elif st == St.Clock and 'Core' in name:
                        out['gpu']['ck'] = val
                    elif st == St.Power:
                        out['gpu'].setdefault('pw', val)
                    elif st == St.Fan:
                        out['gpu'].setdefault('fan', val)
                    elif st == St.Control and 'fan' in name.lower():
                        out['gpu'].setdefault('fan', val)
                elif ht in ('Motherboard', 'SuperIO'):
                    if st == St.Temperature:
                        out['host'].setdefault('tp', val)
                    elif st == St.Fan:
                        out['host'].setdefault('fan', val)
                elif ht == 'Storage':
                    if st == St.Temperature:
                        out['disk'].setdefault('tp', val)
        if cpu_fq_max is not None:
            out['cpu']['fq'] = round(cpu_fq_max)
    except Exception as e:
        print(f"[lhm] 读取异常：{e}")
    return out

# ── psutil 采集（始终可用）────────────────────────────────────

_prev = {'net': None, 'disk': None, 't': None}  # 速率差分用
_net_base = None                                # 日流量基线 (sent, recv, 'YYYY-MM-DD')

def get_uptime_str():
    up = int(time.time() - psutil.boot_time())
    d, h, m = up // 86400, (up % 86400) // 3600, (up % 3600) // 60
    if d > 0:  return f"{d}d {h}h"
    if h > 0:  return f"{h}h {m}m"
    return f"{m}m"

def _linux_cpu_temp():
    try:
        for p in ('/sys/class/thermal/thermal_zone0/temp', '/sys/class/thermal/thermal_zone1/temp'):
            if os.path.exists(p):
                with open(p) as f:
                    return round(int(f.read().strip()) / 1000.0)
    except Exception:
        pass
    return None

def _primary_nic():
    try:
        for name, st in psutil.net_if_stats().items():
            if st.isup and name.lower() not in ('lo',) and 'loopback' not in name.lower():
                return name
    except Exception:
        pass
    return ''

def _load_traffic_baseline():
    global _net_base
    try:
        with open(TRAFFIC_FILE) as f:
            d = json.load(f)
            _net_base = (d['sent'], d['recv'], d['date'])
    except Exception:
        _net_base = None

def _save_traffic_baseline():
    try:
        with open(TRAFFIC_FILE, 'w') as f:
            json.dump({'sent': _net_base[0], 'recv': _net_base[1], 'date': _net_base[2]}, f)
    except Exception:
        pass

def collect_psutil(now_ts, dt):
    global _net_base
    d = {'cpu': {}, 'gpu': {}, 'host': {}, 'disk': {}, 'net': {}, 'traf': {}}

    d['cpu']['ld'] = round(psutil.cpu_percent(interval=None), 1)  # 非阻塞：自上次调用以来
    try:
        f = psutil.cpu_freq()
        d['cpu']['fq'] = round(f.current) if f else None
    except Exception:
        d['cpu']['fq'] = None
    d['cpu']['co'] = psutil.cpu_count(logical=True)

    vm = psutil.virtual_memory()
    d['host']['mem'] = round(vm.percent, 1)
    try:
        d['host']['swp'] = round(psutil.swap_memory().percent, 1)
    except Exception:
        d['host']['swp'] = None
    d['host']['up'] = get_uptime_str()

    drive = config.get('system_drive') or ('C:\\' if platform.system() == 'Windows' else '/')
    try:
        d['disk']['use'] = round(psutil.disk_usage(drive).percent, 1)
    except Exception:
        d['disk']['use'] = None

    nio = psutil.net_io_counters()
    dio = psutil.disk_io_counters()
    if _prev['t'] and dt > 0:
        if dio and _prev['disk']:
            d['disk']['rd'] = round((dio.read_bytes  - _prev['disk'].read_bytes)  / dt / 1048576.0, 1)
            d['disk']['wr'] = round((dio.write_bytes - _prev['disk'].write_bytes) / dt / 1048576.0, 1)
        if nio and _prev['net']:
            d['net']['dn'] = round((nio.bytes_recv - _prev['net'].bytes_recv) / dt / 1048576.0, 2)
            d['net']['up'] = round((nio.bytes_sent - _prev['net'].bytes_sent) / dt / 1048576.0, 2)
    _prev['net'], _prev['disk'], _prev['t'] = nio, dio, now_ts
    d['net']['nic'] = _primary_nic()

    today = datetime.now().strftime('%Y-%m-%d')
    if nio:
        if _net_base is None or _net_base[2] != today or nio.bytes_sent < _net_base[0]:
            _net_base = (nio.bytes_sent, nio.bytes_recv, today)  # 首次 / 过午夜 / 计数器回绕 → 重置基线
            _save_traffic_baseline()
        d['traf']['up'] = round(max(0, nio.bytes_sent - _net_base[0]) / 1048576.0, 1)
        d['traf']['dn'] = round(max(0, nio.bytes_recv - _net_base[1]) / 1048576.0, 1)

    if platform.system() == 'Linux':
        t = _linux_cpu_temp()
        if t is not None:
            d['cpu']['tp'] = t
    return d

def build_stats():
    now_ts = time.time()
    dt = (now_ts - _prev['t']) if _prev['t'] else 0
    base = collect_psutil(now_ts, dt)
    for cat, fields in read_lhm().items():       # LHM 覆盖（温度/风扇/GPU/功耗/频率）
        for k, v in fields.items():
            if v is not None:
                base[cat][k] = v
    now = datetime.now()
    av = {'cpu': True, 'disk': True, 'host': True, 'net': True, 'traf': True,
          'gpu': base['gpu'].get('ld') is not None}
    return {
        'v': 2,
        'clk': {'h': now.hour, 'm': now.minute},
        'av': av,
        'cpu': base['cpu'], 'gpu': base['gpu'], 'host': base['host'],
        'disk': base['disk'], 'net': base['net'], 'traf': base['traf'],
    }

# ── 缓存 + 采样线程 ────────────────────────────────────────────

latest_stats = {'v': 2, 'clk': {'h': 0, 'm': 0},
                'av': {'cpu': False, 'gpu': False, 'host': False, 'disk': False, 'net': False, 'traf': False}}
stats_lock = threading.Lock()

def sampler_loop():
    psutil.cpu_percent(interval=None)  # 预热（首次返回 0）
    while True:
        try:
            s = build_stats()
            with stats_lock:
                global latest_stats
                latest_stats = s
        except Exception as e:
            print(f"[sampler] {e}")
        time.sleep(max(1, int(config.get('refresh_interval', 2))))

@app.route('/stats.json')
def get_stats():
    with stats_lock:
        return jsonify(latest_stats)

@app.route('/')
def index():
    return '''
    <html><head><title>Clawd Mochi Monitor</title></head>
    <body style="background:#1c1c20;color:#e8e4dc;font-family:monospace;padding:20px">
    <h1 style="color:#ff4000">Clawd Mochi PC Monitor</h1>
    <p>Status: Running (schema v2)</p>
    <p>API: <a href="/stats.json" style="color:#3FB984">/stats.json</a></p>
    </body></html>
    '''

# ── 开机自启 ────────────────────────────────────────────────────

APP_NAME = "ClawdMochiMonitor"
SCRIPT_PATH = os.path.abspath(__file__)

def is_autostart_enabled():
    system = platform.system()
    if system == 'Windows':
        try:
            import winreg
            key = winreg.OpenKey(winreg.HKEY_CURRENT_USER,
                r"Software\Microsoft\Windows\CurrentVersion\Run", 0, winreg.KEY_READ)
            try:
                winreg.QueryValueEx(key, APP_NAME)
                winreg.CloseKey(key)
                return True
            except FileNotFoundError:
                winreg.CloseKey(key)
                return False
        except Exception:
            return False
    elif system == 'Darwin':
        return os.path.exists(os.path.expanduser("~/Library/LaunchAgents/com.clawd-mochi.monitor.plist"))
    elif system == 'Linux':
        return os.path.exists(os.path.expanduser("~/.config/autostart/clawd-mochi-monitor.desktop"))
    return False

def get_executable_path():
    if getattr(sys, 'frozen', False):
        return sys.executable
    # 依赖只装在脚本同级 .venv；裸 pythonw 会落到无 psutil 的基础 python3 → 开机自启启动即崩。
    # 故优先用 venv 的无窗口 pythonw（存在才用），回退当前解释器。
    venv_pyw = os.path.join(BASE_DIR, '.venv', 'Scripts', 'pythonw.exe')
    exe = venv_pyw if os.path.exists(venv_pyw) else sys.executable
    return f'"{exe}" "{SCRIPT_PATH}"'

def enable_autostart():
    system = platform.system()
    if system == 'Windows':
        try:
            import winreg
            key = winreg.OpenKey(winreg.HKEY_CURRENT_USER,
                r"Software\Microsoft\Windows\CurrentVersion\Run", 0, winreg.KEY_WRITE)
            winreg.SetValueEx(key, APP_NAME, 0, winreg.REG_SZ, get_executable_path())
            winreg.CloseKey(key)
            print(f"Autostart enabled: {get_executable_path()}")
            return True
        except Exception as e:
            print(f"Enable autostart error: {e}")
            return False
    elif system == 'Darwin':
        try:
            import plistlib
            plist_path = os.path.expanduser("~/Library/LaunchAgents/com.clawd-mochi.monitor.plist")
            data = {"Label": "com.clawd-mochi.monitor",
                    "ProgramArguments": [sys.executable, SCRIPT_PATH],
                    "RunAtLoad": True, "KeepAlive": False}
            os.makedirs(os.path.dirname(plist_path), exist_ok=True)
            with open(plist_path, 'wb') as f:
                plistlib.dump(data, f)
            return True
        except Exception as e:
            print(f"Enable autostart error: {e}")
            return False
    elif system == 'Linux':
        try:
            desktop_path = os.path.expanduser("~/.config/autostart/clawd-mochi-monitor.desktop")
            content = f'''[Desktop Entry]
Type=Application
Name=Clawd Mochi Monitor
Exec={sys.executable} "{SCRIPT_PATH}"
Icon={os.path.join(ICON_DIR, "android-chrome-512x512.png")}
Terminal=false
Hidden=false
'''
            os.makedirs(os.path.dirname(desktop_path), exist_ok=True)
            with open(desktop_path, 'w') as f:
                f.write(content)
            return True
        except Exception as e:
            print(f"Enable autostart error: {e}")
            return False
    return False

def disable_autostart():
    system = platform.system()
    if system == 'Windows':
        try:
            import winreg
            key = winreg.OpenKey(winreg.HKEY_CURRENT_USER,
                r"Software\Microsoft\Windows\CurrentVersion\Run", 0, winreg.KEY_WRITE)
            try:
                winreg.DeleteValue(key, APP_NAME)
            except FileNotFoundError:
                pass
            winreg.CloseKey(key)
            return True
        except Exception as e:
            print(f"Disable autostart error: {e}")
            return False
    elif system == 'Darwin':
        plist_path = os.path.expanduser("~/Library/LaunchAgents/com.clawd-mochi.monitor.plist")
        try:
            if os.path.exists(plist_path):
                os.remove(plist_path)
            return True
        except Exception as e:
            print(f"Disable autostart error: {e}")
            return False
    elif system == 'Linux':
        desktop_path = os.path.expanduser("~/.config/autostart/clawd-mochi-monitor.desktop")
        try:
            if os.path.exists(desktop_path):
                os.remove(desktop_path)
            return True
        except Exception as e:
            print(f"Disable autostart error: {e}")
            return False
    return False

def toggle_autostart(icon, item):
    if is_autostart_enabled():
        disable_autostart()
    else:
        enable_autostart()
    update_menu()

# ── 系统托盘 ────────────────────────────────────────────────────

tray_icon = None

def get_local_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"

def get_autostart_text():
    return f"开机自启  {'[✓ 已启用]' if is_autostart_enabled() else '[未启用]'}"

def _backend_text():
    if platform.system() != 'Windows':
        return "后端：psutil"
    if LHM_AVAILABLE:
        return f"后端：LHM{'(管理员)' if ADMIN else '(非管理员·温度受限)'}"
    return "后端：psutil(无 LHM)"

def _build_menu():
    return pystray.Menu(
        pystray.MenuItem("状态：运行中 ✅", None, enabled=False),
        pystray.MenuItem(f"IP：{get_local_ip()}:{PORT}", None, enabled=False),
        pystray.MenuItem(_backend_text(), None, enabled=False),
        pystray.Menu.SEPARATOR,
        pystray.MenuItem(get_autostart_text(), toggle_autostart),
        pystray.Menu.SEPARATOR,
        pystray.MenuItem("退出", exit_app)
    )

def update_menu():
    if tray_icon is None:
        return
    tray_icon.menu = _build_menu()
    tray_icon.update_menu()

def exit_app(icon, item):
    global tray_icon
    try:
        if _computer is not None:
            _computer.Close()
    except Exception:
        pass
    if tray_icon:
        tray_icon.stop()
    os._exit(0)

def load_icon():
    system = platform.system()
    if system == 'Windows':
        p = os.path.join(ICON_DIR, 'favicon.ico')
        if os.path.exists(p):
            return Image.open(p)
    else:
        p = os.path.join(ICON_DIR, 'android-chrome-512x512.png')
        if os.path.exists(p):
            return Image.open(p).resize((64, 64), Image.Resampling.LANCZOS)
    return Image.new('RGB', (64, 64), color='#ff4000')

def run_tray():
    global tray_icon
    if not TRAY_AVAILABLE:
        return
    tray_icon = pystray.Icon("clawd_mochi_monitor", load_icon(), "Clawd Mochi Monitor", _build_menu())
    tray_icon.run()

def run_flask():
    app.run(host='0.0.0.0', port=PORT, threaded=True, use_reloader=False)

# ── 主程序 ──────────────────────────────────────────────────────

def main():
    print("Clawd Mochi PC Monitor (v2) starting...")
    print(f"Port: {PORT}  Platform: {platform.system()}  Autostart: {is_autostart_enabled()}")

    init_lhm()
    _load_traffic_baseline()

    threading.Thread(target=sampler_loop, daemon=True).start()
    threading.Thread(target=run_flask, daemon=True).start()
    print(f"HTTP API: http://{get_local_ip()}:{PORT}/stats.json  ({_backend_text()})")

    if TRAY_AVAILABLE:
        run_tray()
    else:
        print("Running without tray icon. Press Ctrl+C to exit.")
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            print("Exiting...")

if __name__ == '__main__':
    main()
