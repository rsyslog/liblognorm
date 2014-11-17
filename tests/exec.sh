test_file=$(basename $1)
test_name=$(echo $test_file | sed -e 's/\..*//g')

echo ===============================================================================
echo "[${test_file}]: test for ${2}"

set -e

function execute() {
    echo $1 | ../src/lognormalizer -r tmp.rulebase -e json 1>test.out 2>test.err
}

function assert_output_contains() {
    cat test.out | grep -F "$1"
}

function reset_rules() {
    rm -f tmp.rulebase
}

function add_rule() {
    echo $1 >> tmp.rulebase
}

reset_rules
