#!/bin/bash
scp ../client/*.json client/
rm -rf ./**.log

for bs in 100000 10000 1000 100 10; do
    for i in {1..5}; do
        bash ./check-port.sh 23333 23338
        ./client/d_raft_launcher_clientless \
            --config-file ./client/config.json \
            --log-level 1 \
            --batch-size $bs \
            --freq 5.0 \
            --size $((bs * 100)) \
            --datadir "./data/$bs-$i"

        sleep 1
        kill -9 $(ps -ef | grep -i '/usr/bin/top' | grep -v grep | awk '{print $2}')
    done
done
