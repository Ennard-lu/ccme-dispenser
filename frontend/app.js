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
const aiCheckModeEl = document.getElementById('ai-check-mode');
const vialGrid = document.getElementById('vial-grid');
const videoMse = document.getElementById('video-mse');
const videoMjpeg = document.getElementById('video-mjpeg');
const videoOverlay = document.getElementById('video-overlay');
const stirrerDetailEl = document.getElementById('stirrer-detail');
const errorBanner = document.getElementById('error-banner');
const errorTitleEl = document.getElementById('error-title');
const errorMessageEl = document.getElementById('error-message');
const errorHintEl = document.getElementById('error-hint');
const errorTimeEl = document.getElementById('error-time');
const btnReset = document.getElementById('btn-reset');

const ERROR_TITLES = {
    ai_not_configured: '视觉模型未配置',
    camera_unavailable: '视觉服务不可用',
    camera_stream_unavailable: '摄像头不可用',
    fmc_unavailable: '三轴控制器离线',
    fmc_home_failed: '三轴回零失败',
    fmc_move_failed: '三轴移动失败',
    pump_start_failed: '注水泵故障',
    dispense_pump_failed: '分装泵故障',
    heater_start_failed: '加热故障',
    stirrer_start_failed: '搅拌故障',
    dissolution_timeout: '溶解检测超时',
    vision_check_failed: '视觉检测失败',
    orchestrator_unavailable: '编排器离线',
    already_running: '流程已在运行',
    not_running: '流程未运行',
    not_in_error: '当前不可重置',
    start_failed: '启动失败',
    stop_failed: '停止失败',
    reset_failed: '重置失败',
    invalid_request: '请求参数错误',
};

const STATE_MAP = {
    idle: '空闲',
    homing: '回零中',
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

    card.title = '';
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

function updateModuleOffline(id, error) {
    const card = document.getElementById(id);
    card.querySelector('.module-status').textContent = '离线';
    card.classList.remove('active');
    card.querySelector('.module-indicator').className = 'module-indicator offline';
    card.title = error || '';
}

// ── Error banner ──

function showError(error) {
    if (!error) {
        errorBanner.classList.add('hidden');
        return;
    }

    errorTitleEl.textContent = ERROR_TITLES[error.code] || '系统错误';
    errorMessageEl.textContent = error.message || '未知错误';
    errorHintEl.textContent = error.hint ? `建议：${error.hint}` : '';
    errorTimeEl.textContent = error.timestamp_ms
        ? new Date(error.timestamp_ms).toLocaleTimeString()
        : '';
    errorBanner.classList.remove('hidden');
}

function clearError() {
    errorBanner.classList.add('hidden');
}

function makeClientError(code, message, hint) {
    return { code, message, hint, timestamp_ms: Date.now() };
}

// ── Fetch status ──

async function fetchStatus() {
    try {
        const resp = await fetch(`${API_BASE}/status`);
        if (!resp.ok) {
            showError(makeClientError(
                'status_unavailable',
                `状态接口返回 HTTP ${resp.status}`,
                '检查 ccme-web 和 nginx 状态'));
            return;
        }
        const data = await resp.json();

        setStatus(data.state);
        currentVialEl.textContent = data.current_vial || 0;
        totalVialsEl.textContent = data.total_vials || 36;
        currentStateEl.textContent = STATE_MAP[data.state] || data.state || '空闲';
        aiCheckModeEl.textContent = data.ai_check_skipped ? '已跳过' : '启用';
        aiCheckModeEl.title = data.ai_check_skipped
            ? 'CCME_SKIP_AI_CHECK 已启用，流程不会等待 AI 溶解判定'
            : '流程会在分装前等待 AI 溶解判定';

        if (data.error && data.error.active !== false) {
            showError(data.error);
        } else if (data.state === 'error') {
            showError(makeClientError(
                'unknown_error', '流程发生未知错误', '查看后端日志'));
        } else {
            clearError();
        }

        if (data.modules) {
            const m = data.modules;

            if (m.pump && m.pump.online !== false) {
                updateModuleStatus('mod-pump', m.pump.running,
                    m.pump.running ? '运行中' : '关闭');
            } else {
                updateModuleOffline('mod-pump', m.pump && m.pump.error);
            }

            if (m.pump2 && m.pump2.online !== false) {
                updateModuleStatus('mod-pump2', m.pump2.running,
                    m.pump2.running ? '运行中' : '关闭');
            } else {
                updateModuleOffline('mod-pump2', m.pump2 && m.pump2.error);
            }

            if (m.stirrer && m.stirrer.online !== false) {
                const s = m.stirrer;
                const parts = [];
                if (s.stirring) parts.push(`${s.speed} RPM`);
                if (s.heating) {
                    parts.push(`${Number(s.set_temp).toFixed(1)}°C / ${Number(s.temp).toFixed(1)}°C`);
                } else if (Number(s.temp) > 0) {
                    parts.push(`${Number(s.temp).toFixed(1)}°C`);
                }
                const label = s.stirring || s.heating ? parts.join(' | ') : '关闭';
                updateModuleStatus('mod-stirrer', s.stirring || s.heating, label);
                document.getElementById('mod-stirrer').title = s.error || '';
                stirrerDetailEl.textContent = '';
            } else {
                updateModuleOffline('mod-stirrer', m.stirrer && m.stirrer.error);
                stirrerDetailEl.textContent = '';
            }

            if (m.camera && m.camera.online !== false) {
                if (!m.camera.connected) {
                    updateModuleOffline('mod-camera', m.camera.error);
                } else {
                    const camLabel = m.camera.ai_configured ? '已连接' : 'AI未配置';
                    updateModuleStatus('mod-camera', true, camLabel);
                    document.getElementById('mod-camera').title = m.camera.error || '';
                }
            } else {
                updateModuleOffline('mod-camera', m.camera && m.camera.error);
            }

            if (m.fmc && m.fmc.online !== false) {
                if (!m.fmc.connected && !m.fmc.moving) {
                    updateModuleOffline('mod-fmc', m.fmc.error || 'FMC 控制器未连接');
                } else {
                    const fmcLabel = m.fmc.moving ? '移动中' : '就绪';
                    updateModuleStatus('mod-fmc', m.fmc.connected || m.fmc.moving, fmcLabel);
                    document.getElementById('mod-fmc').title = m.fmc.error || '';
                }
            } else {
                updateModuleOffline('mod-fmc', m.fmc && m.fmc.error);
            }
        }

        updateVialGrid(data.total_vials || 36, data.current_vial || 0,
            data.state || 'idle');
    } catch (e) {
        console.error('Status fetch failed:', e);
        showError(makeClientError(
            'web_unavailable',
            '无法连接后端服务',
            '检查 ccme-web、nginx 和网络连接'));
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
        const num = document.createElement('span');
        num.className = 'vial-cell-num';
        num.textContent = i + 1;
        cell.appendChild(num);

        if (i < current) {
            cell.classList.add('filled');
        } else if (i === current && state !== 'idle' && state !== 'complete') {
            cell.classList.add('current');
        }

        vialGrid.appendChild(cell);
    }
}

// ── Workflow ──

async function readApiError(resp) {
    let data = null;
    try {
        data = await resp.json();
    } catch (e) {
        // Response was not JSON.
    }

    if (data && data.error) {
        return data.error;
    }

    return makeClientError(
        'http_error',
        (data && data.message) || `请求失败（HTTP ${resp.status}）`,
        '查看后端日志');
}

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
            showError(await readApiError(resp));
            return;
        }

        const data = await resp.json().catch(() => ({ success: true }));
        if (data.success === false) {
            showError(data.error || makeClientError(
                'start_failed', '启动失败', '查看后端日志'));
            return;
        }

        clearError();
        startPolling();
        fetchStatus();
    } catch (e) {
        console.error('Start failed:', e);
        showError(makeClientError(
            'web_unavailable', '无法连接后端服务', '检查 ccme-web、nginx 和网络连接'));
    }
}

async function stopWorkflow() {
    try {
        const resp = await fetch(`${API_BASE}/stop`, { method: 'POST' });
        if (!resp.ok) {
            showError(await readApiError(resp));
            return;
        }
        fetchStatus();
    } catch (e) {
        console.error('Stop failed:', e);
        showError(makeClientError(
            'web_unavailable', '无法连接后端服务', '检查 ccme-web、nginx 和网络连接'));
    }
}

async function resetWorkflow() {
    btnReset.disabled = true;
    try {
        const resp = await fetch(`${API_BASE}/reset`, { method: 'POST' });
        if (!resp.ok) {
            showError(await readApiError(resp));
            return;
        }
        clearError();
        fetchStatus();
    } catch (e) {
        console.error('Reset failed:', e);
        showError(makeClientError(
            'web_unavailable', '无法连接后端服务', '检查 ccme-web、nginx 和网络连接'));
    } finally {
        btnReset.disabled = false;
    }
}

// ── Polling ──

function startPolling() {
    if (pollTimer) return;
    pollTimer = setInterval(fetchStatus, 500);
    fetchStatus();
}

// ── Video ──

let mseStartTimer = null;
let mjpegStartTimer = null;
let usingMjpeg = false;

function showVideoOverlay(text) {
    videoOverlay.classList.remove('hidden');
    videoOverlay.querySelector('span').textContent = text;
}

function hideVideoOverlay() {
    videoOverlay.classList.add('hidden');
}

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
        mpegtsPlayer.play().catch(() => fallbackToMjpeg());
        mpegtsPlayer.on(mpegts.Events.ERROR, () => fallbackToMjpeg());

        // If no playable frame arrives in time, fall back to MJPEG instead of
        // leaving the overlay on "连接中" forever.
        mseStartTimer = setTimeout(() => {
            if (videoMse.readyState < 3) {
                fallbackToMjpeg();
            }
        }, 8000);
    } else {
        fallbackToMjpeg();
    }
}

function fallbackToMjpeg() {
    if (usingMjpeg) return;
    usingMjpeg = true;
    clearTimeout(mseStartTimer);

    if (mpegtsPlayer) { mpegtsPlayer.destroy(); mpegtsPlayer = null; }
    videoMse.style.display = 'none';
    videoMjpeg.style.display = 'block';
    showVideoOverlay('连接中...');

    if (!videoMjpeg.getAttribute('src')) {
        videoMjpeg.src = `${API_BASE}/stream`;
    }

    clearTimeout(mjpegStartTimer);
    mjpegStartTimer = setTimeout(() => {
        if (!videoMjpeg.complete || videoMjpeg.naturalWidth === 0) {
            showVideoOverlay('视频流连接超时，请检查推流服务');
        }
    }, 10000);
}

videoMjpeg.addEventListener('load', () => {
    clearTimeout(mjpegStartTimer);
    hideVideoOverlay();
});
videoMjpeg.addEventListener('error', () => {
    clearTimeout(mjpegStartTimer);
    showVideoOverlay('视频流不可用');
});
videoMse.addEventListener('playing', () => {
    clearTimeout(mseStartTimer);
    hideVideoOverlay();
});

// ── Init ──

btnStart.addEventListener('click', startWorkflow);
btnStop.addEventListener('click', stopWorkflow);
btnReset.addEventListener('click', resetWorkflow);

fetchStatus();
updateVialGrid(36, 0, 'idle');
startPolling();
initVideoStream();
