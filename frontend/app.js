const API_BASE = '/api';

const statusEl = document.getElementById('status-indicator');
const statusTextEl = document.getElementById('status-text');
const volumeInput = document.getElementById('volume-input');
const stirSpeedInput = document.getElementById('stir-speed-input');
const heatTempInput = document.getElementById('heat-temp-input');
const dispenseVolumeInput = document.getElementById('dispense-volume-input');
const btnStart = document.getElementById('btn-start');
const btnStop = document.getElementById('btn-stop');
const currentVialEl = document.getElementById('current-vial');
const totalVialsEl = document.getElementById('total-vials');
const currentStateEl = document.getElementById('current-state');
const vialGrid = document.getElementById('vial-grid');
const videoMse = document.getElementById('video-mse');
const videoMjpeg = document.getElementById('video-mjpeg');
const videoOverlay = document.getElementById('video-overlay');
const stirrerDetailEl = document.getElementById('stirrer-detail');

const STATE_MAP = {
    idle: '空闲',
    injecting_water: '注水中',
    heating: '加热中',
    stirring: '搅拌中',
    checking_dissolution: '检测溶解',
    dispensing: '转移中',
    moving_to_vial: '移动平台',
    complete: '已完成',
    error: '错误',
    unknown: '未知',
};

let pollTimer = null;
let mpegtsPlayer = null;

// ── Left panel view switching ──

document.querySelectorAll('.view-btn').forEach(btn => {
    btn.addEventListener('click', () => {
        document.querySelectorAll('.view-btn').forEach(b => b.classList.remove('active'));
        document.querySelectorAll('.left-view').forEach(v => v.classList.remove('active'));
        btn.classList.add('active');
        document.getElementById('view-' + btn.dataset.view).classList.add('active');
    });
});

// ── Region focus ──

const regions = Array.from(document.querySelectorAll('[data-region]'));
let focusedRegion = null;

function setFocusedRegion(region) {
    if (focusedRegion === region) return;
    regions.forEach(r => r.classList.toggle('region-focused', r === region));
    focusedRegion = region;
}

document.addEventListener('pointerdown', (e) => {
    const region = e.target.closest('[data-region]');
    if (region) setFocusedRegion(region);
});

setFocusedRegion(document.querySelector('[data-region="left"]'));

// ── Status ──

function setStatus(state) {
    statusEl.className = 'status-badge';
    if (state === 'idle') {
        statusEl.classList.add('status-idle');
        statusTextEl.textContent = '空闲';
        btnStart.disabled = false;
        btnStop.disabled = true;
    } else if (state === 'complete') {
        statusEl.classList.add('status-complete');
        statusTextEl.textContent = '已完成';
        btnStart.disabled = false;
        btnStop.disabled = true;
    } else if (state === 'error') {
        statusEl.classList.add('status-error');
        statusTextEl.textContent = '错误';
        btnStart.disabled = false;
        btnStop.disabled = true;
    } else {
        statusEl.classList.add('status-running');
        statusTextEl.textContent = STATE_MAP[state] || state;
        btnStart.disabled = true;
        btnStop.disabled = false;
    }
}

function updateModuleStatus(id, running, label) {
    const card = document.getElementById(id);
    const sEl = card.querySelector('.module-status');
    const indicator = card.querySelector('.module-indicator');

    if (running) {
        card.classList.add('active');
        sEl.textContent = label || '运行中';
        indicator.className = 'module-indicator active';
    } else {
        card.classList.remove('active');
        sEl.textContent = label || '关闭';
        indicator.className = 'module-indicator online';
    }
}

function updateModuleOffline(id) {
    const card = document.getElementById(id);
    card.querySelector('.module-status').textContent = '离线';
    card.classList.remove('active');
    card.querySelector('.module-indicator').className = 'module-indicator offline';
}

// ── Fetch status ──

async function fetchStatus() {
    try {
        const resp = await fetch(`${API_BASE}/status`);
        if (!resp.ok) return;
        const data = await resp.json();

        setStatus(data.state);
        currentVialEl.textContent = data.current_vial || 0;
        totalVialsEl.textContent = data.total_vials || 36;
        currentStateEl.textContent = STATE_MAP[data.state] || data.state || '空闲';

        if (data.modules) {
            const m = data.modules;

            if (m.pump) {
                updateModuleStatus('mod-pump', m.pump.running,
                    m.pump.running ? '运行中' : '关闭');
            } else {
                updateModuleOffline('mod-pump');
            }

            if (m.pump2) {
                updateModuleStatus('mod-pump2', m.pump2.running,
                    m.pump2.running ? '运行中' : '关闭');
            } else {
                updateModuleOffline('mod-pump2');
            }

            if (m.stirrer) {
                const s = m.stirrer;
                const parts = [];
                if (s.stirring) parts.push(`${s.speed} RPM`);
                if (s.heating) parts.push(`${s.set_temp.toFixed(1)}°C / ${s.temp.toFixed(1)}°C`);
                else if (s.temp > 0) parts.push(`${s.temp.toFixed(1)}°C`);
                const label = s.stirring || s.heating ? parts.join(' | ') : '关闭';
                updateModuleStatus('mod-stirrer', s.stirring || s.heating, label);
                stirrerDetailEl.textContent = '';
            } else {
                updateModuleOffline('mod-stirrer');
                stirrerDetailEl.textContent = '';
            }

            if (m.camera) {
                updateModuleStatus('mod-camera', m.camera.connected,
                    m.camera.connected ? '已连接' : '离线');
            } else {
                updateModuleOffline('mod-camera');
            }

            if (m.fmc) {
                const fmcLabel = m.fmc.moving ? '移动中'
                    : m.fmc.connected ? '就绪' : '离线';
                updateModuleStatus('mod-fmc', m.fmc.connected || m.fmc.moving, fmcLabel);
            } else {
                updateModuleOffline('mod-fmc');
            }
        }

        updateVialGrid(data.total_vials || 36, data.current_vial || 0,
            data.state || 'idle');
    } catch (e) {
        console.error('Status fetch failed:', e);
    }
}

// ── Vial grid ──

function updateVialGrid(total, current, state) {
    vialGrid.innerHTML = '';
    const cols = Math.ceil(Math.sqrt(total));
    vialGrid.style.gridTemplateColumns = `repeat(${cols}, 1fr)`;

    for (let i = 0; i < total; i++) {
        const cell = document.createElement('div');
        cell.className = 'vial-cell';
        cell.textContent = i + 1;

        if (i < current) {
            cell.classList.add('filled');
        } else if (i === current && state !== 'idle' && state !== 'complete') {
            cell.classList.add('current');
        }

        vialGrid.appendChild(cell);
    }
}

// ── Workflow ──

async function startWorkflow() {
    const volume = parseFloat(volumeInput.value);
    if (isNaN(volume) || volume <= 0) { alert('请输入有效的注水量'); return; }

    const stirSpeed = parseInt(stirSpeedInput.value, 10);
    if (isNaN(stirSpeed) || stirSpeed < 0) { alert('请输入有效的搅拌转速'); return; }

    const heatTemp = parseFloat(heatTempInput.value);
    if (isNaN(heatTemp) || heatTemp < 0) { alert('请输入有效的加热温度'); return; }

    const dispenseVolume = parseFloat(dispenseVolumeInput.value);
    if (isNaN(dispenseVolume) || dispenseVolume <= 0) { alert('请输入有效的转移量'); return; }

    try {
        const resp = await fetch(`${API_BASE}/start`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                volume_ml: volume,
                stir_speed_rpm: stirSpeed,
                heat_temp_c: heatTemp,
                dispense_volume_ml: dispenseVolume,
            }),
        });
        if (!resp.ok) {
            const err = await resp.json();
            alert(`启动失败: ${err.message || resp.statusText}`);
            return;
        }
        startPolling();
    } catch (e) {
        console.error('Start failed:', e);
    }
}

async function stopWorkflow() {
    try {
        await fetch(`${API_BASE}/stop`, { method: 'POST' });
    } catch (e) {
        console.error('Stop failed:', e);
    }
}

// ── Polling ──

function startPolling() {
    if (pollTimer) return;
    pollTimer = setInterval(fetchStatus, 1000);
    fetchStatus();
}

// ── Video ──

function initVideoStream() {
    videoMjpeg.style.display = 'none';
    videoMse.style.display = 'block';

    if (typeof mpegts !== 'undefined' && mpegts.isSupported()) {
        mpegtsPlayer = mpegts.createPlayer({
            type: 'mpegts',
            url: `${API_BASE}/hls/stream`
        }, {
            enableWorker: true,
            enableStashBuffer: false,
            stashInitialSize: 128,
            autoCleanupSourceBuffer: true,
            autoCleanupMaxBackwardDuration: 5,
            autoCleanupMinBackwardDuration: 3,
            liveBufferLatencyChasing: true,
            liveBufferLatencyMaxLatency: 1.5,
            liveBufferLatencyMinRemain: 0.3,
        });
        mpegtsPlayer.attachMediaElement(videoMse);
        mpegtsPlayer.load();
        mpegtsPlayer.play().then(() => {
            videoOverlay.classList.add('hidden');
        }).catch(() => fallbackToMjpeg());
        mpegtsPlayer.on(mpegts.Events.ERROR, () => fallbackToMjpeg());
    } else {
        fallbackToMjpeg();
    }
}

function fallbackToMjpeg() {
    if (mpegtsPlayer) { mpegtsPlayer.destroy(); mpegtsPlayer = null; }
    videoMse.style.display = 'none';
    videoMjpeg.style.display = 'block';
}

videoMjpeg.addEventListener('load', () => videoOverlay.classList.add('hidden'));
videoMjpeg.addEventListener('error', () => {
    videoOverlay.classList.remove('hidden');
    videoOverlay.querySelector('span').textContent = '视频流不可用';
});
videoMse.addEventListener('playing', () => videoOverlay.classList.add('hidden'));

// ── Init ──

btnStart.addEventListener('click', startWorkflow);
btnStop.addEventListener('click', stopWorkflow);

fetchStatus();
updateVialGrid(36, 0, 'idle');
startPolling();
initVideoStream();
