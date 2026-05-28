const statusEl       = document.getElementById('status');
const bgColorEl      = document.getElementById('bg-color');
const penColorEl     = document.getElementById('pen-color');
const backlightEl    = document.getElementById('backlight');
const clearCanvasBtn = document.getElementById('clear-canvas');
const termInputEl    = document.getElementById('term-input');
const padEl          = document.getElementById('pad');
const padCtx         = padEl.getContext('2d');

let ws = null;
let reconnectTimer = null;

function setConnState(cls, text) {
    statusEl.className = cls;
    statusEl.textContent = text;
}

function send(msg) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(msg));
    }
}

function setCanvasOnly(enabled) {
    document.querySelectorAll('.canvas-only').forEach(el => {
        el.classList.toggle('disabled', !enabled);
    });
}

function setClaudeOnly(enabled) {
    document.querySelectorAll('.claude-only').forEach(el => {
        el.classList.toggle('disabled', !enabled);
    });
}

function clearPad(hex) {
    padCtx.fillStyle = hex || '#FFFFFF';
    padCtx.fillRect(0, 0, padEl.width, padEl.height);
}

function applyState(s) {
    document.querySelectorAll('.mode-btn').forEach(btn => {
        btn.classList.toggle('active', btn.dataset.mode === s.mode);
    });
    document.querySelectorAll('.speed-btn').forEach(btn => {
        btn.classList.toggle('active', parseInt(btn.dataset.speed) === s.speed);
    });
    // 检测 bg_color 是否变化（与本地 input 当前值比较；hex 大小写归一）
    const bgChanged = s.bg_color &&
        s.bg_color.toLowerCase() !== bgColorEl.value.toLowerCase();
    if (s.bg_color)  bgColorEl.value  = s.bg_color;
    if (s.pen_color) penColorEl.value = s.pen_color;
    backlightEl.checked = !!s.backlight;
    setCanvasOnly(s.mode === 'canvas');
    setClaudeOnly(s.mode === 'claude_code');
    // BG 变化时同步清本地 pad（设备端已被 set_bg_color / clear_canvas 清屏）
    if (bgChanged) clearPad(s.bg_color);
}

function connect() {
    clearTimeout(reconnectTimer);
    setConnState('connecting', 'connecting...');
    ws = new WebSocket(`ws://${location.host}/ws`);
    ws.onopen = () => setConnState('connected', 'connected');
    ws.onclose = () => {
        setConnState('disconnected', 'disconnected');
        reconnectTimer = setTimeout(connect, 5000);
    };
    ws.onerror = () => setConnState('disconnected', 'disconnected');
    ws.onmessage = (e) => {
        try {
            const msg = JSON.parse(e.data);
            if (msg.type === 'state') applyState(msg);
        } catch (err) {
            console.warn('bad msg', e.data, err);
        }
    };
}

// === Event wiring ===
document.querySelectorAll('.mode-btn').forEach(btn => {
    btn.addEventListener('click', () => send({ type: 'set_mode', id: btn.dataset.mode }));
});
document.querySelectorAll('.speed-btn').forEach(btn => {
    btn.addEventListener('click', () => send({ type: 'set_speed', v: parseInt(btn.dataset.speed) }));
});
bgColorEl.addEventListener('change',  () => send({ type: 'set_bg_color',  hex: bgColorEl.value  }));
penColorEl.addEventListener('change', () => send({ type: 'set_pen_color', hex: penColorEl.value }));
backlightEl.addEventListener('change', () => send({ type: 'set_backlight', on: backlightEl.checked }));
clearCanvasBtn.addEventListener('click', () => {
    send({ type: 'clear_canvas', hex: bgColorEl.value });
    clearPad(bgColorEl.value);
});

termInputEl.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
        send({ type: 'terminal_input', c: '\n' });
        termInputEl.value = '';
        e.preventDefault();
    } else if (e.key === 'Backspace') {
        send({ type: 'terminal_input', c: '\b' });
    } else if (e.key.length === 1) {
        send({ type: 'terminal_input', c: e.key });
    }
});

// === Drawing Pad（Phase 7）===
let drawing = false;
let prevPad = null;
let lastSendMs = 0;
const SEND_INTERVAL_MS = 50;

function padCoords(e) {
    const rect = padEl.getBoundingClientRect();
    const sx = padEl.width  / rect.width;
    const sy = padEl.height / rect.height;
    return {
        x: Math.round((e.clientX - rect.left) * sx),
        y: Math.round((e.clientY - rect.top)  * sy),
    };
}

function localDraw(x, y, prev) {
    padCtx.strokeStyle = penColorEl.value;
    padCtx.fillStyle   = penColorEl.value;
    padCtx.lineWidth   = 3;
    padCtx.lineCap     = 'round';
    if (prev) {
        padCtx.beginPath();
        padCtx.moveTo(prev.x, prev.y);
        padCtx.lineTo(x, y);
        padCtx.stroke();
    } else {
        padCtx.beginPath();
        padCtx.arc(x, y, 1.5, 0, Math.PI * 2);
        padCtx.fill();
    }
}

padEl.addEventListener('pointerdown', (e) => {
    if (padEl.parentElement.classList.contains('disabled')) return;
    drawing = true;
    padEl.setPointerCapture(e.pointerId);
    const p = padCoords(e);
    localDraw(p.x, p.y, null);
    send({ type: 'stroke', x: p.x, y: p.y });
    prevPad = p;
    lastSendMs = Date.now();
});

padEl.addEventListener('pointermove', (e) => {
    if (!drawing) return;
    const p = padCoords(e);
    localDraw(p.x, p.y, prevPad);
    const now = Date.now();
    if (now - lastSendMs >= SEND_INTERVAL_MS) {
        send({ type: 'stroke', x: p.x, y: p.y, prev_x: prevPad.x, prev_y: prevPad.y });
        prevPad = p;
        lastSendMs = now;
    }
});

function endStroke(e) {
    if (!drawing) return;
    drawing = false;
    try { padEl.releasePointerCapture(e.pointerId); } catch {}
    const p = padCoords(e);
    if (prevPad) {
        send({ type: 'stroke', x: p.x, y: p.y, prev_x: prevPad.x, prev_y: prevPad.y });
    }
    prevPad = null;
}
padEl.addEventListener('pointerup', endStroke);
padEl.addEventListener('pointercancel', () => { drawing = false; prevPad = null; });

// 初始铺白底
clearPad('#FFFFFF');

connect();
