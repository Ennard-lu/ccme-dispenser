#!/bin/bash
set -e

BIN_DIR="${HOME}/.local/bin"
LOG_DIR="${HOME}/.log/ccme"

mkdir -p "$LOG_DIR"

echo "Starting CCME Dispenser services..."

declare -A SVC_BIN=(
    [pump]="ccme-pump"
    [stirrer]="ccme-stirrer"
    [camera]="ccme-camera"
    [fmc]="ccme-fmc"
    [web]="ccme-web"
    [orchestrator]="ccme-dispenser"
)

declare -A SVC_PRI=(
    [pump]="sudo sh -c"
    [stirrer]="sudo sh -c"
    [camera]="sh -c"
    [fmc]="sh -c"
    [web]="sh -c"
    [orchestrator]="sh -c"
)

sudo ${BIN_DIR}/start-ip.sh || echo "ip already set"
nohup sudo ${BIN_DIR}/gstreamer-stream.sh > "${LOG_DIR}/gstreamer.log" 2>&1 &

sleep 3

for svc in pump stirrer camera fmc web orchestrator; do
    binary="${BIN_DIR}/${SVC_BIN[$svc]}"
    if [ ! -f "$binary" ]; then
        echo "ERROR: $binary not found"
        exit 1
    fi
    echo "Starting ${binary}..."
    ${SVC_PRI[$svc]} "$binary" > "${LOG_DIR}/${svc}.log" 2>&1 &
    echo "  PID: $!"
done

echo "All services started. Logs in ${LOG_DIR}/"
echo "Press Ctrl+C to stop all services."

trap 'echo "Stopping..."; kill -9 $(jobs -p) 2>/dev/null; exit 0' INT TERM

wait
