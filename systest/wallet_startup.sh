#!/bin/bash
# Starts the Qt wallet without a display on a fresh data directory, again on the same one, after its config was deleted and
# after it was emptied; every start must get as far as starting the node, open the same wallet and account, and persist them
source "$(dirname "$BASH_SOURCE")/lib/common.sh"

if [ -z "${NANO_WALLET_EXE-}" ]; then
    echo "Skipped: NANO_WALLET_EXE is not set"
    exit 0
fi

DATADIR=$(new_datadir)
CONFIG=$DATADIR/config-qtwallet.toml
ZERO_ACCOUNT=nano_1111111111111111111111111111111111111111111111111111hifc8npp

fail() {
    echo "FAIL: $1" >&2
    exit 1
}

# Starts the wallet and stops it once it has started its node, which it only does after loading and persisting its config;
# the argument names the start in failure messages
start_wallet() {
    # The console log is flushed line by line, unlike the log file, which only flushes errors right away
    local start=$1 out=$DATADIR/wallet.out pid
    QT_QPA_PLATFORM=offscreen "$NANO_WALLET_EXE" --network dev --data_path "$DATADIR" > "$out" 2>&1 &
    pid=$!
    register_pid "$pid"
    for ((i = 0; i < 120; i++)); do
        if grep -q "Peering port" "$out"; then
            kill "$pid"
            wait "$pid" 2>/dev/null || true
            return 0
        fi
        # An error dialog keeps the process alive, so a critical log line is the only sign of a refused start
        if grep -q "\[critical\]" "$out"; then
            grep "\[critical\]" "$out" >&2
            fail "the wallet refused to start ($start)"
        fi
        if ! kill -0 "$pid" 2>/dev/null; then
            tail -n 20 "$out" >&2
            fail "the wallet exited before starting its node ($start)"
        fi
        sleep 0.5
    done
    tail -n 20 "$out" >&2
    fail "timed out waiting for the wallet to start its node ($start)"
}

# The wallet and account the last start opened, as logged by the wallet: "<wallet id> <account>"
opened() {
    sed -n 's/.*Opened wallet \([0-9A-F]*\) with account \(nano_[a-z0-9]*\).*/\1 \2/p' "$DATADIR/wallet.out"
}

# The wallet and account the persisted config names: "<wallet id> <account>"
configured() {
    echo "$(sed -n 's/^wallet = "\(.*\)"$/\1/p' "$CONFIG") $(sed -n 's/^account = "\(.*\)"$/\1/p' "$CONFIG")"
}

# A fresh install creates a wallet and an account, opens them and persists them in the config
start_wallet "fresh install"
grep -q "Created wallet" "$DATADIR/wallet.out" || fail "a fresh install did not create a wallet"
first=$(opened)
[ -n "$first" ] || fail "the wallet did not log which wallet and account it opened"
[ "${first#* }" != "$ZERO_ACCOUNT" ] || fail "the wallet opened no account"
[ -f "$CONFIG" ] || fail "the wallet config was not written"
[ "$(configured)" = "$first" ] || fail "the wallet config names $(configured), the wallet opened $first"
cp "$CONFIG" "$DATADIR/config-first-start.toml"

# A restart opens the same wallet and account and leaves the config unchanged
start_wallet "restart"
[ "$(opened)" = "$first" ] || fail "the restart opened $(opened) instead of $first"
cmp -s "$CONFIG" "$DATADIR/config-first-start.toml" || fail "the wallet config changed on restart"

# A start after the config was deleted recovers the same wallet and account from the wallet store and persists them again
rm "$CONFIG"
start_wallet "config deleted"
[ "$(opened)" = "$first" ] || fail "the start without a config opened $(opened) instead of $first"
[ "$(configured)" = "$first" ] || fail "the recreated wallet config names $(configured) instead of $first"

# A start with an empty config, as an interrupted write leaves behind, recovers them as well
: > "$CONFIG"
start_wallet "config emptied"
[ "$(opened)" = "$first" ] || fail "the start with an empty config opened $(opened) instead of $first"
[ "$(configured)" = "$first" ] || fail "the rewritten wallet config names $(configured) instead of $first"

echo "PASS"
