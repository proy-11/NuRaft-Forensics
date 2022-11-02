#!/bin/bash
scp ../client/*.json client/
rm -rf ./**.log

bash ./check-port.sh 23333 23338

./client/d_raft_launcher_clientless --config-file ./client/config.json --log-level 1 --batch-size 1000000 --freq 10.0 --size 200000000 --datadir 'data/debug'
