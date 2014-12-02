test_file=$(basename $1)
test_name=$(echo $test_file | sed -e 's/\..*//g')

echo ===============================================================================
echo "[${test_file}]: test for ${2}"

set -e
if [ "x$debug" == "xon" ]; then #get core-dump on crash
    ulimit -c unlimited
fi

cmd=../src/ln_test

source ./options.sh

function execute() {
    if [ "x$debug" == "xon" ]; then
	echo "======rulebase======="
	cat tmp.rulebase
	echo "====================="
	set -x
    fi
    echo $1 | $cmd $ln_opts -r tmp.rulebase -e json > test.out 
    echo "Out:"
    cat test.out
    if [ "x$debug" == "xon" ]; then
	set +x
    fi
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
