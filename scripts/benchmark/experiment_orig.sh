#!/bin/bash

rm -rf ./*.log*
payload=$1

handler() {
    bash stop.sh
    kill -SIGINT $$
    exit
}

trap handler SIGINT

for comp in 0 1 2 3; do
    for freq in 100 200 500 1000 2000 5000; do
        for i in {1..5}; do
            bash ./bench.sh $freq "$payload" "$comp" "$i"
            echo "finished comp $comp, frequency $freq, set $i"
            sleep 1.5
        done
    done
done
