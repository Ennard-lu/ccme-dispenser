const API_BASE = '/api';

const statusEl = document.getElementById('status-indicator');
const statusTextEl = document.getElementById('status-text');
const volumeInput = document.getElementById('volume-input');
const btnStart = document.getElementById('btn-start');
const btnStop = document.getElementById('btn-stop');
const currentVialEl = document.getElementById('current-vial');
const totalVialsEl = document.getElementById('total-vials');
const currentStateEl = document.getElementById('current-state');
const vialGrid = document.getElementById('vial-grid');
const videoStream = document.getElementById('video-stream');
const videoOverlay = document.getElementById('video-overlay');
const stirrerDetailEl = document.getElementById('stirrer-detail');

let pollTimer = null;

function setStatus(state) {
    statusEl.className = 'status-badge';
    if (state === 'idle') {
        statusEl.classList.add('status-idle');
        statusTextEl.textContent = 'Idle';
        btnStart.disabled = false;
        btnStop.disabled = true;
    } else if (state === 'complete') {
        statusEl.classList.add('status-complete');
        statusTextEl.textContent = 'Complete';
        btnStart.disabled = false;
        btnStop.disabled = true;
    } else if (state === 'error') {
        statusEl.classList.add('status-error');
        statusTextEl.textContent = 'Error';
        btnStart.disabled = false;
        btnStop.disabled = true;
    } else {
        statusEl.classList.add('status-running');
        statusTextEl.textContent = state.replace(/_/g, ' ');
        btnStart.disabled = true;
        btnStop.disabled = false;
    }
}

function updateModuleStatus(id, running, label) {
    const card = document.getElementById(id);
    const statusEl = card.querySelector('.module-status');
    const indicator = card.querySelector('.module-indicator');

    if (running) {
        card.classList.add('active');
        statusEl.textContent = label || 'ON';
        indicator.className = 'module-indicator active';
    } else {
        card.classList.remove('active');
        statusEl.textContent = label || 'OFF';
        indicator.className = 'module-indicator online';
    }
}

function updateModuleOffline(id) {
    const card = document.getElementById(id);
    const statusEl = card.querySelector('.module-status');
    const indicator = card.querySelector('.module-indicator');
    card.classList.remove('active');
    statusEl.textContent = 'Offline';
    indicator.className = 'module-indicator offline';
}

async function fetchStatus() {
    try {
        const resp = await fetch(`${API_BASE}/status`);
        if (!resp.ok) return;
        const data = await resp.json();

        setStatus(data.state);
        currentVialEl.textContent = data.current_vial || 0;
        totalVialsEl.textContent = data.total_vials || 16;
        currentStateEl.textContent = (data.state || 'idle').replace(/_/g, ' ');

        if (data.modules) {
            const m = data.modules;

            if (m.pump) {
                updateModuleStatus('mod-pump', m.pump.running,
                    m.pump.running ? 'Running' : 'OFF');
            } else {
                updateModuleOffline('mod-pump');
            }

            if (m.pump2) {
                updateModuleStatus('mod-pump2', m.pump2.running,
                    m.pump2.running ? 'Running' : 'OFF');
            } else {
                updateModuleOffline('mod-pump2');
            }

            if (m.stirrer) {
                const s = m.stirrer;
                const parts = [];
                if (s.stirring) parts.push(`${s.speed} RPM`);
                if (s.heating) parts.push(`${s.set_temp.toFixed(1)}°C / ${s.temp.toFixed(1)}°C`);
                else if (s.temp > 0) parts.push(`${s.temp.toFixed(1)}°C`);
                const label = s.stirring || s.heating ? parts.join(' | ') : 'OFF';
                updateModuleStatus('mod-stirrer', s.stirring || s.heating, label);
                stirrerDetailEl.textContent = parts.length > 0 ? '' : '';
            } else {
                updateModuleOffline('mod-stirrer');
                stirrerDetailEl.textContent = '';
            }

            if (m.camera) {
                updateModuleStatus('mod-camera', m.camera.connected,
                    m.camera.connected ? 'Connected' : 'Offline');
            } else {
                updateModuleOffline('mod-camera');
            }

            if (m.fmc) {
                const fmcLabel = m.fmc.moving ? 'Moving'
                    : m.fmc.connected ? 'Ready' : 'Offline';
                updateModuleStatus('mod-fmc', m.fmc.connected || m.fmc.moving,
                    fmcLabel);
            } else {
                updateModuleOffline('mod-fmc');
            }
        }

        updateVialGrid(data.total_vials || 16, data.current_vial || 0,
            data.state || 'idle');
    } catch (e) {
        console.error('Status fetch failed:', e);
    }
}

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

async function startWorkflow() {
    const volume = parseFloat(volumeInput.value);
    if (isNaN(volume) || volume <= 0) {
        alert('Please enter a valid volume.');
        return;
    }

    try {
        const resp = await fetch(`${API_BASE}/start`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ volume_ml: volume }),
        });
        if (!resp.ok) {
            const err = await resp.json();
            alert(`Failed to start: ${err.message || resp.statusText}`);
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

function startPolling() {
    if (pollTimer) return;
    pollTimer = setInterval(fetchStatus, 1000);
    fetchStatus();
}

function stopPolling() {
    if (pollTimer) {
        clearInterval(pollTimer);
        pollTimer = null;
    }
}

videoStream.addEventListener('load', () => {
    videoOverlay.classList.add('hidden');
});

videoStream.addEventListener('error', () => {
    videoOverlay.classList.remove('hidden');
    videoOverlay.querySelector('span').textContent = 'Stream unavailable';
});

btnStart.addEventListener('click', startWorkflow);
btnStop.addEventListener('click', stopWorkflow);

fetchStatus();
updateVialGrid(16, 0, 'idle');
startPolling();
