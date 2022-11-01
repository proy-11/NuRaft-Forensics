#!/bin/bash
scp ../client/*.json client/

for bs in 5 10; do
    for i in {1..2}; do
        ./client/d_raft_launcher \
            --config-file ./client/config.json \
            --log-level 1 \
            --batch-size $bs \
            --size $((bs * 300)) \
            --datadir "../data/$bs-$i"
    done
done
