#!/bin/bash

test_bin=$(dirname $0)/../../build/tests/split_brain_attack_test
datetime=$(date +%Y-%m-%d-%H-%M-%S)
branch_name=`git --git-dir=$(dirname $0)/../../.git branch --show-current`
output_dir="$(dirname $0)/../../test_output/raft-$branch_name-split-brain-$datetime"
mkdir -p $output_dir

# run the test
R_OUT_PATH=$output_dir $test_bin > $output_dir/test.log 2>&1

# process results
output=$(python $(dirname $0)/../../scripts/forensics/commit_cert.py $output_dir/forensics_out)

# check the last line of the output
if [[ $(tail -n 1 <<< "$output") == "Commit certs are different." ]]; then
    echo " Found evidence of split brain attack. [ PASSED ]"
else
    echo " No evidence of split brain attack. [ FAILED ]"
fi
