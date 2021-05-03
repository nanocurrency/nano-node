#!/bin/sh

# the caller should set the env var NANO_NODE_EXE to point to the nano_node executable
# if NANO_NODE_EXE is unser ot empty then "../../build/nano_node" is used
NANO_NODE_EXE=${NANO_NODE_EXE:-../../build/nano_node}

mkdir -p data/log
rm data/log/log_*.log

# start nano_node and store its pid so we can later send it
# the SIGHUP signal and so we can terminate it
echo start nano_node
$NANO_NODE_EXE --daemon --data_path data &
pid=$!
echo pid=$pid

# wait for the node to start-up
sleep 2

# set bandwidth params 42 and 43 in the config file
cat > data/config-node.toml <<EOF
[node]
bandwidth_limit = 42
bandwidth_limit_burst_ratio = 43
EOF

# send nano_node the SIGHUP signal
kill -HUP $pid

# wait for the signal handler to kick in
sleep 2

# set another set of bandwidth params 44 and 45 in the config file
cat > data/config-node.toml <<EOF
[node]
bandwidth_limit = 44
bandwidth_limit_burst_ratio = 45
EOF

# send nano_node the SIGHUP signal
kill -HUP $pid

# wait for the signal handler to kick in
sleep 2

# terminate nano node and wait for it to die
kill $pid
wait $pid

# the bandwidth params are logged in logger and we check for that logging below

# check that the first signal handler got run and the data is correct
grep -q "set_bandwidth_params(42, 43)" data/log/log_*.log
rc1=$?
echo rc1=$rc1

# check that the second signal handler got run and the data is correct
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
