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

let pollTimer = null;

function setStatus(state) {
    statusEl.className = '';
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
        statusTextEl.textContent = state;
        btnStart.disabled = true;
        btnStop.disabled = false;
    }
}

async function fetchStatus() {
    try {
        const resp = await fetch(`${API_BASE}/status`);
        if (!resp.ok) return;
        const data = await resp.json();

        setStatus(data.state);
        currentVialEl.textContent = data.current_vial;
        totalVialsEl.textContent = data.total_vials;
        currentStateEl.textContent = data.state;

        updateVialGrid(data.total_vials, data.current_vial, data.state);
    } catch (e) {
        console.error('Failed to fetch status:', e);
    }
}

function updateVialGrid(total, current, state) {
    vialGrid.innerHTML = '';
    const cols = 4;
    const rows = Math.ceil(total / cols);

    for (let r = 0; r < rows; r++) {
        for (let c = 0; c < cols; c++) {
            const idx = r * cols + c;
            const cell = document.createElement('div');
            cell.className = 'vial-cell';
            cell.textContent = idx + 1;

            if (idx < current) {
                cell.classList.add('filled');
            } else if (idx === current && state !== 'idle' && state !== 'complete') {
                cell.classList.add('current');
            }

            vialGrid.appendChild(cell);
        }
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
        console.error('Failed to start workflow:', e);
    }
}

async function stopWorkflow() {
    try {
        await fetch(`${API_BASE}/stop`, { method: 'POST' });
    } catch (e) {
        console.error('Failed to stop workflow:', e);
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

btnStart.addEventListener('click', startWorkflow);
btnStop.addEventListener('click', stopWorkflow);

fetchStatus();
vialGrid.innerHTML = '';
for (let i = 0; i < 16; i++) {
    const cell = document.createElement('div');
    cell.className = 'vial-cell';
    cell.textContent = i + 1;
    vialGrid.appendChild(cell);
}
