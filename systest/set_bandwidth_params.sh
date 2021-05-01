#!/bin/sh

NANO_NODE_EXE=${NANO_NODE_EXE:-../../build/nano_node}

mkdir -p data/log
rm data/log/log_*.log

echo start nano_node
$NANO_NODE_EXE --daemon --data_path data &
pid=$!
echo pid=$pid

sleep 2

cat > data/config-node.toml <<EOF
[node]
bandwidth_limit = 42
bandwidth_limit_burst_ratio = 43
EOF

kill -HUP $pid

sleep 2

cat > data/config-node.toml <<EOF
[node]
bandwidth_limit = 44
bandwidth_limit_burst_ratio = 45
EOF

kill -HUP $pid
sleep 2

kill $pid
wait $pid

grep -q "set_bandwidth_params(42, 43)" data/log/log_*.log
rc1=$?
echo rc1=$rc1

grep -q "set_bandwidth_params(44, 45)" data/log/log_*.log
rc2=$?
echo rc2=$rc2

if [ $rc1 -eq 0 -a $rc2 -eq 0 ]; then
    echo set_bandwith_params PASSED
    exit 0
else
    echo set_bandwith_params FAILED
    exit 1
fi
