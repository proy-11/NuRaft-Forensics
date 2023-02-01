#!/bin/bash

if [ $# -lt 4 ]; then
    freq=2000
    payload=1
    complexity=1
    workdir="orig_debug"
    duration=10
    printf "setting default params\n\tfreq: \t\t%s\n\tpayload: \t%s\n" $freq $payload
else
    freq=$1
    payload=$2
    complexity=$3
    workdir="forensics-lv$complexity-$payload/$freq-$4"
    duration=20
fi

sleeper=2
tail=8
gap=20
cthreads=5

ips=""

for VAR in {2..5}; do
    newip="127.0.0.1:$((43332 + VAR))"
    tmux send-keys -t "02:$((VAR - 1))" "./tests/raft_bench $VAR $newip $((duration + sleeper + gap)) $complexity $workdir &" Enter
    tmux send-keys -t "02:$((VAR - 1))" "mypid=\$!" Enter
    tmux send-keys -t "02:$((VAR - 1))" "start=\$(date +%s)" Enter
    ips="$ips $newip"
done

echo "initiated followers, waiting..."
sleep $sleeper

echo "initiating leader"
tmux send-keys -t "02:0" "./tests/raft_bench 1 127.0.0.1:43333 $duration $complexity $workdir $freq $cthreads $payload $ips" Enter

sleep $duration
sleep $tail
echo "terminating"

for VAR in {2..5}; do
    tmux send-keys -t "02:$((VAR - 1))" "kill -9 \$mypid" Enter
    tmux send-keys -t "02:$((VAR - 1))" "end=\$(date +%s)" Enter
    tmux send-keys -t "02:$((VAR - 1))" "echo \"killed after \$((end - start)) seconds\"" Enter
done
