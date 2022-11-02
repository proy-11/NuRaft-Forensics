#!/bin/bash

for ((PORT = $1; PORT <= $2; PORT++)); do
    read -r PID <<<"$(lsof -nP -iTCP -sTCP:LISTEN | grep "$PORT" | awk '/d_raft[[:space:]]/ { print $2 }')"

    if [ -n "$PID" ]; then
        kill -9 "$PID"
        echo "killed process $PID occupying $PORT"
    fi
done
