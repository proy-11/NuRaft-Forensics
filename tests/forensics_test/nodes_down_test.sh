#!/bin/bash

USER=ubuntu
IPS=("127.0.0.1" "127.0.0.1" "127.0.0.1")
PORT=33445
raft_bench_bin=$(dirname $0)/../../build/tests/raft_forensics_bench
datetime=$(date +%Y-%m-%d-%H-%M-%S)
branch_name=`git --git-dir=$(dirname $0)/../../.git branch --show-current`

freq=100
payload=4096
complexity=3
workdir="$(dirname $0)/../../test_output/raft-$branch_name-lv$complexity-$payload-$datetime/$freq"

# 0s launched
# 10s follower freeze
# 30s follower comes back
# 40s leader freeze
# 60s leader comes back
# 70s stop and check disk file validity 

dur_1=10
dur_2=20
dur_3=10
dur_4=20
dur_5=10

duration=$((dur_1 + dur_2 + dur_3 + dur_4 + dur_5))

sleeper=2
tail=8
gap=20
cthreads=1
parallel=500

ips=""
id=2

tmux new-session -d -s raft
mkdir -p $workdir


for IP in "${IPS[@]}"; do
    echo "$id: $IP"
    PORTF=$((PORT + id))
    tmux new-window -t raft -n $((id - 1)) -d
    tmux send-keys -t "raft:$((id - 1))" "$raft_bench_bin $id 127.0.0.1:$PORTF $((duration + sleeper + gap)) $complexity $workdir" Enter
    id=$((id + 1))
    ips="$ips $IP:$PORTF"
    echo "$id"
done

sleep $sleeper

tmux send-keys -t "raft:0" "$raft_bench_bin 1 127.0.0.1:$PORT \
    $duration $complexity $workdir $freq $cthreads $parallel $payload $ips" Enter

sleep $dur_1
# follower 1 (server 2) freeze
tmux send-keys -t "raft:1" C-z
sleep $dur_2
# follower 1 (server 2) comes back
tmux send-keys -t "raft:1" fg Enter
sleep $dur_3
# leader (server 1) freeze
tmux send-keys -t "raft:0" C-z
sleep $dur_4
# leader (server 1) comes back
tmux send-keys -t "raft:0" fg Enter
sleep $dur_5



sleep $tail

echo -e "0 output:\n" > $workdir/main.log
tmux capture-pane -p -t raft:0 -S - -E - >> $workdir/main.log

id=2
for IP in "${IPS[@]}"; do
    tmux send-keys -t raft:$((id - 1)) C-c
    echo -e "$((id - 1)) output:\n" >> $workdir/main.log
    tmux capture-pane -p -t raft:$((id - 1)) -S - -E - >> $workdir/main.log
    id=$((id + 1))
done

echo "Terminal output is saved in $workdir/main.log"

echo -n "Test consistency of log entries"

any_difference=false
for i in $(seq 1 $((id - 1))); do
    if ! diff -q $workdir/log_entries_p1.dat $workdir/log_entries_p$i.dat; then
        any_difference=true
    fi
done

if [ "$any_difference" = false ]; then
    echo " [ PASS ]"
else
    echo " [ FAILED ]"
fi

echo -n "Test consistency of election lists"

# Capture the output of the command
output=$(python $(dirname $0)/../../scripts/forensics/election_list.py $workdir/forensics_out)

# check the last line of the output
if [[ $(tail -n 1 <<< "$output") == "All election lists are the same." ]]; then
    echo " [ PASS ]"
else
    echo " [ FAILED ]"
fi


echo -n "Test leader signatures of all terms"


# Capture the output of the command
output=$(python $(dirname $0)/../../scripts/forensics/leader_signatures.py $workdir/forensics_out)

# check the last line of the output
if [[ $(tail -n 1 <<< "$output") == "All leader sigs of all term are the same." ]]; then
    echo " [ PASS ]"
else
    echo " [ FAILED ]"
fi

tmux kill-session -t raft
