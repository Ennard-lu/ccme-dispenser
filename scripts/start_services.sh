#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
LOG_DIR="/var/log/ccme"

mkdir -p "$LOG_DIR"

echo "Starting CCME Dispenser services..."

# Start each service in background
for svc in pump stirrer camera fmc web orchestrator; do
    binary="${BUILD_DIR}/ccme-${svc}"
    if [ ! -f "$binary" ]; then
        echo "ERROR: $binary not found"
        exit 1
    fi
    echo "Starting ${svc}..."
    "$binary" > "${LOG_DIR}/${svc}.log" 2>&1 &
    echo "  PID: $!"
done

echo "All services started. Logs in ${LOG_DIR}/"
echo "Press Ctrl+C to stop all services."

# Trap to kill all services on exit
trap 'echo "Stopping..."; kill $(jobs -p) 2>/dev/null; exit 0' INT TERM

wait
