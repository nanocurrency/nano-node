#!/bin/bash
# Shared helpers for systest scripts; source this file, do not execute it
# Requires NANO_NODE_EXE to point at the nano_node executable

set -eu

: "${NANO_NODE_EXE:?NANO_NODE_EXE must point to the nano_node executable}"

# Parent of every data directory from new_datadir, which callers run in a $(...) subshell where it cannot record them in a variable
SYSTEST_DATADIRS=$(mktemp -d)

# Creates a temporary data directory registered for cleanup on exit
# Set KEEP_DATADIR=1 to preserve directories for debugging; on failure the node log tails are printed
new_datadir() {
    mktemp -d "$SYSTEST_DATADIRS/datadir.XXXXXX"
}

# Registers a background process to be killed on exit
register_pid() {
    SYSTEST_PIDS="${SYSTEST_PIDS-} $1"
}

# Waits for a process to exit, killing it with SIGKILL when it is still running after the timeout; usage: wait_for_exit <pid> [timeout_seconds]
wait_for_exit() {
    local pid=$1 seconds=${2-30} i
    for ((i = 0; i < seconds * 2; i++)); do
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.5
    done
    if kill -0 "$pid" 2>/dev/null; then
        echo "Process $pid did not exit within $seconds seconds, killing it" >&2
        kill -9 "$pid" 2>/dev/null || true
    fi
    # Reaps the process when it is a child of this shell
    wait "$pid" 2>/dev/null || true
}

# Exit trap: stops registered processes and waits for them to exit, dumps log tails from the data directories on failure, then removes them unless KEEP_DATADIR=1
systest_cleanup() {
    local status=$?
    for pid in ${SYSTEST_PIDS-}; do
        kill "$pid" 2>/dev/null || true
    done
    # A process keeps writing to its data directory until it exits
    for pid in ${SYSTEST_PIDS-}; do
        wait_for_exit "$pid"
    done
    for dir in "$SYSTEST_DATADIRS"/*; do
        [ -d "$dir" ] || continue
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
    # Fails while preserved data directories are left inside
    rmdir "$SYSTEST_DATADIRS" 2>/dev/null || true
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
