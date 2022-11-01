#!/bin/bash
scp ../client/*.json client/
rm -rf ./**.log

for bs in 100000; do
    for i in {2..2}; do
        bash ./check-port.sh 23333 23338
        ./client/d_raft_launcher_clientless \
            --config-file ./client/config.json \
            --log-level 1 \
            --batch-size $bs \
            --freq 5.0 \
            --size $((bs * 100)) \
            --datadir "./data/$bs-$i"

        sleep 1
    done
done

# gdb --args ./client/d_raft_launcher_clientless \
#     --config-file ./client/config.json \
#     --log-level 1 \
#     --batch-size 100 \
#     --freq 5.0 \
#     --size 3000 \
#     --datadir './data/debug'

# ./client/d_raft_launcher_clientless --config-file ./client/config.json --log-level 1 --batch-size 1000 --freq 10.0 --size 600000 --datadir './data/debug'
