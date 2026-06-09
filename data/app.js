const statusEl       = document.getElementById('status');
const bgColorEl      = document.getElementById('bg-color');
const penColorEl     = document.getElementById('pen-color');
const backlightEl    = document.getElementById('backlight');
const clearCanvasBtn = document.getElementById('clear-canvas');
const termInputEl    = document.getElementById('term-input');
const padEl          = document.getElementById('pad');
const padCtx         = padEl.getContext('2d');
const ccBadgeEl      = document.getElementById('cc-badge');
const faceIdEl       = document.getElementById('face-id');
const remTimeEl      = document.getElementById('rem-time');
const remMsgEl       = document.getElementById('rem-msg');
const remDailyEl     = document.getElementById('rem-daily');
const remAddBtn      = document.getElementById('rem-add');
const remListEl      = document.getElementById('rem-list');
const monModeBtns    = document.querySelectorAll('.mon-mode-btn');
const monIntervalEl  = document.getElementById('mon-interval');
const monCatEl       = document.getElementById('mon-cat');
const monCatsEl      = document.getElementById('mon-cats');
const ccScopeInfoEl  = document.getElementById('cc-scope-info');
const pcClockEl      = document.getElementById('pc-clock');
const pcStatsEl      = document.getElementById('pc-stats');
const pcHintEl       = document.getElementById('pc-hint');
const gifFileEl      = document.getElementById('gif-file');
const gifUploadBtn   = document.getElementById('gif-upload');
const gifListEl      = document.getElementById('gif-list');
const gifHintEl      = document.getElementById('gif-hint');

let ws = null;
let lastMode = '';
let reconnectTimer = null;
let pcIp = '';
let pcPollTimer = null;

// Phase 15：PC 实时镜像强调色（与设备 pc_monitor.cpp kAccent565 / spec §2 同源）
const MON_ACCENT = {
    cpu: '#ff4000', gpu: '#A47CF0', host: '#5AA0E0',
    disk: '#F2B544', net: '#3FB984', traf: '#4ECDC4',
};

const faces = [
    { key: 'face_wuyu',      label: '无语 / Deadpan' },
    { key: 'face_wenhao',    label: '问号 / Question' },
    { key: 'face_gantanhao', label: '感叹号 / Alert' },
    { key: 'face_angry',     label: '生气 / Angry' },
    { key: 'face_yes',       label: '对号 / Yes' },
    { key: 'face_X',         label: '叉叉 / No' },
    { key: 'face_glass',     label: '墨镜 / Glasses' },
    { key: 'anim_jiyanjing', label: '挤眼睛 / Wink' },
    { key: 'anim_yun',       label: '晕晕 / Dizzy' },
    { key: 'anim_close',     label: '闭眼睛 / Close' },
    { key: 'anim_dead',      label: '死掉了 / Dead' },
    { key: 'anim_dian',      label: '等等 / Wait' },
    { key: 'anim_smile',     label: '笑笑 / Smile' },
    { key: 'anim_look',      label: '看你 / Look' },
    { key: 'anim_hart',      label: '心跳 / Heart' },
    { key: 'anim_zzz',       label: '睡着了 / Sleep' },
    { key: 'anim_ganga',     label: '尴尬 / Awkward' },
    { key: 'anim_idle',      label: '待机 / Idle' },
];

// Phase 14b：PC Monitor 分类（key 须与 ws_protocol kMonCatNames 同序）
const monCats = [
    { key: 'cpu',  label: 'CPU' },
    { key: 'disk', label: 'Disk' },
    { key: 'gpu',  label: 'GPU' },
    { key: 'host', label: 'Host' },
    { key: 'net',  label: 'Network' },
    { key: 'traf', label: 'Traffic' },
];

function setConnState(cls, text) {
    statusEl.className = cls;
    statusEl.textContent = text;
}

function send(msg) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(msg));
    }
}

// Phase 15：Tab 内联容器 —— 只显当前 mode 的 .panel（data-panel 支持空格分隔多 mode）
function showPanel(mode) {
    document.querySelectorAll('.panel').forEach(p => {
        p.classList.toggle('active', p.dataset.panel.split(' ').includes(mode));
    });
    if (mode === 'pc_monitor') { if (!pcPollTimer) startPcPoll(); }
    else stopPcPoll();
}

// === PC Monitor 实时镜像（网页直接 fetch PC 服务，服务已开 CORS）===
function startPcPoll() {
    stopPcPoll();
    fetchPcStats();
    pcPollTimer = setInterval(fetchPcStats, 3000);
}
function stopPcPoll() {
    if (pcPollTimer) { clearInterval(pcPollTimer); pcPollTimer = null; }
}
function showPcHint(text, clear) {
    pcHintEl.textContent = text;
    pcHintEl.classList.remove('hide');
    if (clear) pcStatsEl.innerHTML = '';
}
function fetchPcStats() {
    if (!pcIp) { showPcHint('Set PC IP in Settings', true); return; }
    const ctrl = new AbortController();
    const timer = setTimeout(() => ctrl.abort(), 2500);
    fetch('http://' + pcIp + ':8080/stats.json', { signal: ctrl.signal, cache: 'no-store' })
        .then(r => r.ok ? r.json() : Promise.reject(r.status))
        .then(d => { clearTimeout(timer); pcHintEl.classList.add('hide'); renderPcStats(d); })
        .catch(() => { clearTimeout(timer); showPcHint('PC offline', false); });
}

function pcNum(v, d) { return (v == null || Number.isNaN(v)) ? '--' : v.toFixed(d == null ? 0 : d); }
function pcTemp(v)   { return (v == null || Number.isNaN(v)) ? '--' : v.toFixed(0) + '°C'; }
function pcGhz(v)    { return (v == null || Number.isNaN(v)) ? '--' : (v / 1000).toFixed(1) + 'GHz'; }
function pcGb(v)     { return (v == null || Number.isNaN(v)) ? '--' : (v / 1024).toFixed(1); }

function pcCard(cat, title, main, pct, rows) {
    const card = document.createElement('div');
    card.className = 'pc-card';
    card.style.setProperty('--acc', MON_ACCENT[cat]);
    const h = document.createElement('div');
    h.className = 'pc-cat';
    h.textContent = title;
    card.appendChild(h);
    const m = document.createElement('div');
    m.className = 'pc-main';
    m.textContent = main;
    card.appendChild(m);
    if (pct != null && !Number.isNaN(pct)) {
        const bar = document.createElement('div');
        bar.className = 'pc-bar';
        const fill = document.createElement('div');
        fill.className = 'pc-bar-fill';
        fill.style.width = Math.max(0, Math.min(100, pct)) + '%';
        bar.appendChild(fill);
        card.appendChild(bar);
    }
    rows.forEach(r => {
        if (!r) return;
        const row = document.createElement('div');
        row.className = 'pc-row';
        row.textContent = r;
        card.appendChild(row);
    });
    return card;
}

// 渲染 6 大分类卡（一屏全展开；不可用类按 av 跳过）
function renderPcStats(d) {
    if (!d) return;
    if (d.clk) pcClockEl.textContent =
        String(d.clk.h).padStart(2, '0') + ':' + String(d.clk.m).padStart(2, '0');
    const av = d.av || {};
    pcStatsEl.innerHTML = '';
    ['cpu', 'gpu', 'host', 'disk', 'net', 'traf'].forEach(cat => {
        if (!av[cat]) return;
        const o = d[cat] || {};
        let title, main, pct = null, rows = [];
        if (cat === 'cpu') {
            title = 'CPU'; pct = o.ld; main = pcNum(o.ld) + '%';
            rows = [pcTemp(o.tp) + '  ' + pcGhz(o.fq),
                    (o.co != null ? o.co + ' cores' : '') + '  ' + pcNum(o.pw) + 'W'];
        } else if (cat === 'gpu') {
            title = 'GPU'; pct = o.ld; main = pcNum(o.ld) + '%';
            rows = [pcTemp(o.tp) + '  ' + (o.ck != null ? pcNum(o.ck) + 'MHz' : '--'),
                    'VRAM ' + pcGb(o.vu) + '/' + pcGb(o.vt) + 'G',
                    pcNum(o.pw) + 'W'];
        } else if (cat === 'host') {
            title = 'Host'; pct = o.mem; main = pcNum(o.mem) + '%';
            rows = ['swap ' + pcNum(o.swp) + '%',
                    pcTemp(o.tp) + (o.fan != null ? '  fan ' + pcNum(o.fan) : ''),
                    o.up ? 'up ' + o.up : ''];
        } else if (cat === 'disk') {
            title = 'Disk'; pct = o.use; main = pcNum(o.use) + '%';
            rows = ['R ' + pcNum(o.rd, 1) + '  W ' + pcNum(o.wr, 1) + ' MB/s', pcTemp(o.tp)];
        } else if (cat === 'net') {
            title = 'Network'; main = '↓' + pcNum(o.dn, 1);
            rows = ['↑' + pcNum(o.up, 1) + ' MB/s', o.nic || ''];
        } else if (cat === 'traf') {
            title = 'Traffic'; main = '↓' + pcGb(o.dn) + 'G';
            rows = ['↑' + pcGb(o.up) + 'G today'];
        }
        pcStatsEl.appendChild(pcCard(cat, title, main, pct, rows));
    });
}

function basename(p) {
    const parts = (p || '').split(/[\\/]/).filter(Boolean);
    return parts.length ? parts[parts.length - 1] : p;
}
function scopeLabel(s) {
    if (!s.cc_scope) {
        const n = (s.cc_projects || []).length;
        return n ? ('All projects · ' + n + ' tracked') : 'All projects';
    }
    return basename(s.cc_scope);
}

function clearPad(hex) {
    padCtx.fillStyle = hex || '#FFFFFF';
    padCtx.fillRect(0, 0, padEl.width, padEl.height);
}

// Phase 13：渲染提醒列表（每次 state 广播重建；删除按钮发 reminder_del）
function renderReminders(list) {
    if (!remListEl) return;
    remListEl.innerHTML = '';
    (list || []).forEach((r, i) => {
        const li = document.createElement('li');
        li.className = 'rem-item';
        const when = document.createElement('span');
        when.className = 'rem-when';
        when.textContent = String(r.hour).padStart(2, '0') + ':' + String(r.minute).padStart(2, '0');
        const text = document.createElement('span');
        text.className = 'rem-text';
        text.textContent = r.msg || '';
        if (r.daily) {
            const tag = document.createElement('span');
            tag.className = 'rem-tag';
            tag.textContent = ' · daily';
            text.appendChild(tag);
        }
        const del = document.createElement('button');
        del.textContent = '✕';
        del.addEventListener('click', () => send({ type: 'reminder_del', index: i }));
        li.appendChild(when);
        li.appendChild(text);
        li.appendChild(del);
        remListEl.appendChild(li);
    });
}

// v0.4.0：GIF 图库渲染（每次 state 广播重建；高亮当前播放槽，Play/✕ 发 WS gif_select/gif_delete）
function gifHint(t) { if (gifHintEl) gifHintEl.textContent = t; }

function renderGifs(s) {
    if (!gifListEl) return;
    const gifs = s.gifs || [];
    const max  = s.gif_max || 4;
    const cur  = s.gif_index;
    gifListEl.innerHTML = '';
    if (!gifs.length) {
        const li = document.createElement('li');
        li.className = 'gif-empty';
        li.textContent = 'No GIFs yet — upload one above.';
        gifListEl.appendChild(li);
    } else {
        gifs.forEach(g => {
            const li = document.createElement('li');
            li.className = 'gif-item' + (g.slot === cur ? ' playing' : '');
            const name = document.createElement('span');
            name.className = 'gif-name';
            name.textContent = 'GIF ' + (g.slot + 1) + ' · ' + Math.round((g.bytes || 0) / 1024) + ' KB';
            const play = document.createElement('button');
            play.textContent = (g.slot === cur) ? '▶ playing' : 'Play';
            play.disabled = (g.slot === cur);
            play.addEventListener('click', () => {
                send({ type: 'gif_select', index: g.slot });
                if (lastMode !== 'gif_player') send({ type: 'set_mode', id: 'gif_player' });
            });
            const del = document.createElement('button');
            del.className = 'gif-del';
            del.textContent = '✕';
            del.addEventListener('click', () => send({ type: 'gif_delete', index: g.slot }));
            li.appendChild(name);
            li.appendChild(play);
            li.appendChild(del);
            gifListEl.appendChild(li);
        });
    }
    gifUploadBtn.disabled = gifs.length >= max;
    if (document.activeElement !== gifFileEl) {
        gifHint(gifs.length >= max
            ? `Gallery full (${max}) — delete one to add more`
            : `≤240px · ≤512KB · ${gifs.length}/${max} used`);
    }
}

// 上传：客户端先校验扩展/大小/尺寸（≤240px），再 POST /gif/upload；设备成功后经 WS 广播刷新列表
function uploadGif() {
    const f = gifFileEl.files && gifFileEl.files[0];
    if (!f) { gifHint('Choose a GIF first'); return; }
    if (!/\.gif$/i.test(f.name) && f.type !== 'image/gif') { gifHint('Not a GIF'); return; }
    if (f.size > 512 * 1024) { gifHint('Too big (max 512KB)'); return; }
    gifUploadBtn.disabled = true;
    gifHint('Checking…');
    const img = new Image();
    const url = URL.createObjectURL(f);
    img.onload = () => {
        URL.revokeObjectURL(url);
        if (img.naturalWidth > 240 || img.naturalHeight > 240) {
            gifHint(`Too large ${img.naturalWidth}×${img.naturalHeight} (max 240px)`);
            gifUploadBtn.disabled = false;
            return;
        }
        gifHint('Uploading…');
        const fd = new FormData();
        fd.append('gif', f, f.name);
        fetch('/gif/upload', { method: 'POST', body: fd })
            .then(r => r.json().catch(() => ({ ok: r.ok })))
            .then(j => {
                gifUploadBtn.disabled = false;
                if (j && j.ok) { gifHint('Uploaded ✓'); gifFileEl.value = ''; }
                else gifHint('Rejected (full / not a GIF / >512KB)');
            })
            .catch(() => { gifUploadBtn.disabled = false; gifHint('Upload failed'); });
    };
    img.onerror = () => { URL.revokeObjectURL(url); gifUploadBtn.disabled = false; gifHint('Not a valid image'); };
    img.src = url;
}

// Phase 14b：PC Monitor 配置 —— 收集控件 → set_monitor
function sendMonitor() {
    const activeBtn = document.querySelector('.mon-mode-btn.active');
    const rotate = activeBtn ? activeBtn.dataset.rotate === '1' : true;
    const interval = parseInt(monIntervalEl.value, 10) || 20;
    const single = monCatEl.value || 'cpu';
    const cats = [...monCatsEl.querySelectorAll('input:checked')].map(c => c.dataset.key);
    send({ type: 'set_monitor', rotate, interval, single, cats });
}

// 设备值回填：模式按钮 / 间隔 / 单显下拉 / 轮询勾选（按 available 置灰）
function renderMonitor(m) {
    if (!m) return;
    monModeBtns.forEach(b => b.classList.toggle('active', (b.dataset.rotate === '1') === !!m.rotate));
    document.querySelector('.mon-rotate-only').classList.toggle('hide', !m.rotate);
    document.querySelector('.mon-single-only').classList.toggle('hide', !!m.rotate);
    if (document.activeElement !== monIntervalEl) monIntervalEl.value = m.interval || 20;
    const avail = m.available || [];
    const cats  = m.cats || [];
    monCatsEl.querySelectorAll('input').forEach(chk => {
        const k = chk.dataset.key;
        chk.checked  = cats.includes(k);
        const ok = avail.includes(k);
        chk.disabled = !ok;
        chk.parentElement.classList.toggle('disabled', !ok);
    });
    if (document.activeElement !== monCatEl) monCatEl.value = m.single || 'cpu';
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
    pcIp = s.pc_ip || '';
    showPanel(s.mode);
    renderMonitor(s.monitor);
    if (s.face_key !== undefined && document.activeElement !== faceIdEl) faceIdEl.value = s.face_key;
    // Phase 11/15：Claude Code 联动状态 pill + 作用域
    if (s.cc_status && ccBadgeEl) {
        ccBadgeEl.textContent = s.cc_status;
        ccBadgeEl.className = 'cc-pill cc-' + s.cc_status;
    }
    if (ccScopeInfoEl) ccScopeInfoEl.textContent = scopeLabel(s);
    // Phase 13：提醒列表
    renderReminders(s.reminders);
    renderGifs(s);                 // v0.4.0：GIF 图库列表 + 高亮
    lastMode = s.mode;
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
    btn.addEventListener('click', () => {
        send({ type: 'set_mode', id: btn.dataset.mode });
        showPanel(btn.dataset.mode);   // 乐观切换，避免等 WS 回包；applyState 回来再对账
    });
});
document.querySelectorAll('.speed-btn').forEach(btn => {
    btn.addEventListener('click', () => send({ type: 'set_speed', v: parseInt(btn.dataset.speed) }));
});
// Phase 12：face_show 表情选择器（用户可见 API 用稳定 key，不暴露 Face 0..16）
faces.forEach(face => {
    const opt = document.createElement('option');
    opt.value = face.key;
    opt.textContent = face.label;
    faceIdEl.appendChild(opt);
});
faceIdEl.addEventListener('change', () => send({ type: 'set_face', key: faceIdEl.value }));
// Phase 14b：PC Monitor 配置控件构建 + 事件
monCats.forEach(c => {
    const opt = document.createElement('option');
    opt.value = c.key;
    opt.textContent = c.label;
    monCatEl.appendChild(opt);
    const lab = document.createElement('label');
    lab.className = 'mon-cat-chk';
    const chk = document.createElement('input');
    chk.type = 'checkbox';
    chk.dataset.key = c.key;
    chk.addEventListener('change', sendMonitor);
    lab.appendChild(chk);
    lab.appendChild(document.createTextNode(' ' + c.label));
    monCatsEl.appendChild(lab);
});
monModeBtns.forEach(b => b.addEventListener('click', () => {
    monModeBtns.forEach(x => x.classList.toggle('active', x === b));
    sendMonitor();
}));
monIntervalEl.addEventListener('change', sendMonitor);
monCatEl.addEventListener('change', sendMonitor);
bgColorEl.addEventListener('change',  () => send({ type: 'set_bg_color',  hex: bgColorEl.value  }));
penColorEl.addEventListener('change', () => send({ type: 'set_pen_color', hex: penColorEl.value }));
backlightEl.addEventListener('change', () => send({ type: 'set_backlight', on: backlightEl.checked }));
// Phase 13：新增提醒（time input "HH:MM" → hour/minute）
remAddBtn.addEventListener('click', () => {
    const t = remTimeEl.value;            // "HH:MM"
    if (!t || t.indexOf(':') < 0) return;
    const parts = t.split(':');
    const h = parseInt(parts[0], 10);
    const m = parseInt(parts[1], 10);
    if (isNaN(h) || isNaN(m)) return;
    send({ type: 'reminder_add', hour: h, minute: m, daily: remDailyEl.checked, msg: remMsgEl.value.trim() });
    remMsgEl.value = '';
});
clearCanvasBtn.addEventListener('click', () => {
    send({ type: 'clear_canvas', hex: bgColorEl.value });
    clearPad(bgColorEl.value);
});
gifUploadBtn.addEventListener('click', uploadGif);

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
    if (!padEl.offsetParent) return;   // canvas 容器隐藏时 offsetParent 为 null，不响应
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
