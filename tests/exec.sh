test_file=$(basename $1)
test_name=$(echo $test_file | sed -e 's/\..*//g')

echo ===============================================================================
echo "[${test_file}]: test for ${2}"

set -e

cmd=../src/ln_test

source ./options.sh

function execute() {
    echo $1 | $cmd $ln_opts -r tmp.rulebase -e json > test.out 
}

function assert_output_contains() {
    cat test.out | grep -F "$1"
}

function assert_output_json_eq() {
    ./json_eq "$1" "$(cat test.out)"
}


function reset_rules() {
    rm -f tmp.rulebase
}

function add_rule() {
    echo $1 >> tmp.rulebase
}

reset_rules
