#!/bin/bash
delay="$1"
shift
while true; do
    printf ".\b"
    sleep 120
done &
timeout -k 5 "$((delay * 60))" "$@"
exit "$?"
