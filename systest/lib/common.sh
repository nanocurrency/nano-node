#!/bin/bash
# Shared helpers for systest scripts; source this file, do not execute it
# Requires NANO_NODE_EXE to point at the nano_node executable

set -eu

: "${NANO_NODE_EXE:?NANO_NODE_EXE must point to the nano_node executable}"

# Creates a temporary data directory registered for cleanup on exit
# Set KEEP_DATADIR=1 to preserve directories for debugging; on failure the node log tails are printed
new_datadir() {
    local dir
    dir=$(mktemp -d)
    SYSTEST_DATADIRS="${SYSTEST_DATADIRS-} $dir"
    echo "$dir"
}

# Registers a background process to be killed on exit
register_pid() {
    SYSTEST_PIDS="${SYSTEST_PIDS-} $1"
}

# Exit trap: kills registered processes, dumps log tails from registered data directories on failure, then removes them unless KEEP_DATADIR=1
systest_cleanup() {
    local status=$?
    for pid in ${SYSTEST_PIDS-}; do
        kill "$pid" 2>/dev/null || true
    done
    for dir in ${SYSTEST_DATADIRS-}; do
        if [ "$status" -ne 0 ]; then
            echo "Systest failed, dumping log tails from $dir" >&2
            tail -n 50 "$dir"/log/log_*.log >&2 2>/dev/null || true
        fi
        if [ "${KEEP_DATADIR-0}" = "1" ]; then
            echo "Preserving data directory: $dir" >&2
        else
            rm -rf "$dir"
        fi
    done
    return "$status"
}
trap systest_cleanup EXIT

# Waits for a file to appear, failing early when the given process dies; usage: wait_for_file <file> [pid] [timeout_seconds]
wait_for_file() {
    local file=$1 pid=${2-} tries=$((${3-30} * 2))
    for ((i = 0; i < tries; i++)); do
        [ -f "$file" ] && return 0
        if [ -n "$pid" ] && ! kill -0 "$pid" 2>/dev/null; then
            echo "FAIL: process exited while waiting for $file" >&2
            return 1
        fi
        sleep 0.5
    done
    echo "FAIL: timed out waiting for $file" >&2
    return 1
}
