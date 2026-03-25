#!/bin/bash
# This file is part of the liblognorm project, released under ASL 2.0

set -e

echo "running test $0"

scriptdir="$(cd "$(dirname "$0")" && pwd)"
tmpdir="$(mktemp -d "$scriptdir/tmp.test.XXXXXX")"
trap 'rm -rf -- "$tmpdir"' EXIT

rulebase="$tmpdir/rules.rb"
input="$tmpdir/input.log"
output="$tmpdir/output.csv"

cat > "$rulebase" <<'EOF'
version=2
rule=:%msg:rest%
EOF

run_case() {
	printf '%s\n' "$2" > "$input"
	"$scriptdir/../src/lognormalizer" -r "$rulebase" -e csv -E "$3" -ooutputCSVNoQuotes \
		< "$input" > "$output"
	actual="$(tr -d '\n' < "$output")"
	if [ "$actual" != "$1" ]; then
		printf 'FAIL: expected <%s>, got <%s>\n' "$1" "$actual"
		exit 1
	fi
}

run_case 'plain value' 'plain value' msg
run_case '"comma,value"' 'comma,value' msg
run_case '"quote\"value"' 'quote"value' msg

cat > "$rulebase" <<'EOF'
version=2
rule=:%flags:repeat{"parser":{"type":"word","name":"."},"while":{"type":"literal","text":" "}}%
EOF

run_case '"[bar,foo]"' 'foo bar' flags
