#!/usr/bin/env python3
# TurboVM vs standard-parser field-type parity audit.
#
# This file is part of the liblognorm project, released under ASL 2.0.
#
# For each (rule, input) it runs the test binary twice, once with the standard
# parser and once with TurboVM, and compares the parsed JSON semantically (so
# key order and whitespace do not matter). It exits non-zero if any field type
# produces different output on the two engines, except for the types listed in
# KNOWN, which TurboVM intentionally approximates.
#
# The turbo side runs -oturbostrict, NOT -oturbo. This matters: with -oturbo,
# ln_normalize_to_str() silently falls back to the recursive walker whenever
# TurboVM declines a message, so a broken turbo opcode still produces the
# walker's (correct) output and every comparison passes. Strict mode reports
# the failure instead, which is what makes a turbo defect visible here.
#
# Tags are excluded from the comparison. The two engines spell them
# differently on purpose (turbo puts "tags" at the root, the ECS spelling; the
# walker adds a flat "event.tags"), which is a serialization difference rather
# than a field-type one, and this auditor is about field types.
#
# Run from a built tree:
#     python3 tests/turbo_parity.py
# Override the binary or library path with the LN / LN_LIBDIR env vars.

import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.dirname(HERE)
LN = os.environ.get("LN", os.path.join(BUILD, "src", "ln_test"))
LIBDIR = os.environ.get("LN_LIBDIR", os.path.join(BUILD, "src", ".libs"))
if os.path.isdir(LIBDIR):
    env_lib = os.environ.get("LD_LIBRARY_PATH", "")
    os.environ["LD_LIBRARY_PATH"] = LIBDIR + (":" + env_lib if env_lib else "")

# TurboVM maps several distinct parser types onto one generic opcode: alpha /
# string / duration / kernel-timestamp / cisco-interface-spec all compile to
# OP_FIELD_WORD, and every date and time parser compiles to OP_FIELD_DATE.
# Where that generic form does not cover the standard parser's grammar, turbo
# declines the message and the walker serves it, so the OUTPUT is still right
# under -oturbo; what is lost is turbo coverage, not correctness. Strict mode
# makes that visible, which is why these are listed rather than silently green.
#
#   alpha      OP_FIELD_WORD runs to whitespace, Alpha stops at the first
#              non-alphabetic byte ("abc123" -> "abc").
#   date-iso   a bare date with no time part is not matched.
#   time-24hr  a bare time with no date part is not matched.
#
# Fixing these means giving OP_FIELD_WORD / OP_FIELD_DATE a parser-kind
# discriminator (aux is free on both) instead of one grammar for all of them.
KNOWN = {"cisco-interface-spec", "duration", "kernel-timestamp",
         "alpha", "date-iso", "time-24hr"}

# (name, rule-body-after-':', input-line[, extra rulebase lines]).
# Use \t / \n escapes in the input.
CASES = [
    ("word",                  "%f:word% z",                 "hello z"),
    ("alpha",                 "%f:alpha%123",               "abc123"),
    ("number",                "%f:number% z",               "12345 z"),
    ("number-fmt",            '%{"name":"f","type":"number","format":"number"}% z', "42 z"),
    ("hexnumber-fmt",         '%{"name":"f","type":"hexnumber","format":"number"}% z', "0xff z"),
    ("float-fmt",             '%{"name":"f","type":"float","format":"number"}% z', "3.14159 z"),
    ("float",                 "%f:float% z",                "3.14 z"),
    ("hexnumber",             "%f:hexnumber% z",            "0xff z"),
    ("char-to",               "%f:char-to:,%,z",            "abc,z"),
    ("char-sep",              "%f:char-sep:,%,z",           "abc,z"),
    ("rest",                  "%f:rest%",                   "everything after"),
    ("quoted-string",         "%f:quoted-string%",          '"hello world"'),
    ("op-quoted-string",      "%f:op-quoted-string%",       "plainword"),
    ("whitespace",            "start%f:whitespace%ok",      "start \\t ok"),
    ("ipv4",                  "%f:ipv4% z",                 "192.0.2.1 z"),
    ("ipv6",                  "%f:ipv6% z",                 "2001:db8::1 z"),
    ("mac48",                 "%f:mac48% z",                "00:11:22:33:44:55 z"),
    ("date-rfc3164",          "%f:date-rfc3164% z",         "Aug 10 12:00:00 z"),
    ("date-rfc5424",          "%f:date-rfc5424% z",         "2026-08-10T12:00:00Z z"),
    ("date-iso",              "%f:date-iso% z",             "2026-08-10 z"),
    ("time-24hr",             "%f:time-24hr% z",            "13:45:00 z"),
    ("duration",              "%f:duration% z",             "01:30:00 z"),
    ("json",                  "%f:json%",                   '{"a":1,"b":[1,2]}'),

    # %.:json% merges the parsed object into the parent rather than storing it
    # under a name. Arrays inside it must stay arrays: flattening them to .0/.1
    # keys is indistinguishable from an object with numeric keys.
    ("json-merge",            "%.:json%",                   '{"a":1}'),
    ("json-merge-array",      "%.:json%",                   '{"c":["x","y"]}'),
    ("json-merge-array-obj",  "%.:json%",       '{"a":{"b":[{"n":"0"},{"n":"1"}]}}'),
    ("json-merge-array-deep", "%.:json%",       '{"a":{"b":{"c":[1,[2,3]]}}}'),

    # Date parsers accept a format option that changes the emitted JSON type.
    ("date-rfc3164-ts-unix",
     '%{"name":"ts","type":"date-rfc3164","format":"timestamp-unix"}% z',
     "Aug 10 12:00:00 z"),
    ("date-rfc3164-ts-unix-ms",
     '%{"name":"ts","type":"date-rfc3164","format":"timestamp-unix-ms"}% z',
     "Aug 10 12:00:00 z"),
    ("date-rfc3164-fmt-string",
     '%{"name":"ts","type":"date-rfc3164","format":"string"}% z',
     "Aug 10 12:00:00 z"),
    ("date-rfc5424-ts-unix",
     '%{"name":"ts","type":"date-rfc5424","format":"timestamp-unix"}% z',
     "2026-08-10T12:00:00Z z"),
    ("date-rfc5424-ts-unix-ms",
     '%{"name":"ts","type":"date-rfc5424","format":"timestamp-unix-ms"}% z',
     "2026-08-10T12:00:00Z z"),
    ("date-rfc5424-fmt-string",
     '%{"name":"ts","type":"date-rfc5424","format":"string"}% z',
     "2026-08-10T12:00:00Z z"),
    ("cee-syslog",            "%f:cee-syslog%",             '@cee: {"a":1}'),
    ("name-value-list",       "%f:name-value-list%",        "a=1 b=2"),
    ("cef",                   "%f:cef%",                    "CEF:0|V|P|1.0|100|n|5|src=1.2.3.4"),
    ("cisco-interface-spec",  "%f:cisco-interface-spec% z", "GigabitEthernet0/1 z"),
    ("iptables",              "%f:iptables%",               "SRC=1.2.3.4 DST=5.6.7.8 LEN=40"),
    ("string",                "%f:string% z",               "value z"),
    ("tokenized",             "%f:tokenized: :number%",     "1 2 3"),
    ("checkpoint-lea",        "%f:checkpoint-lea%",         "a=1; b=2;"),

    # string-to: the delimiter is a STRING, not a character. The anchored
    # shape below passes even when the delimiter is ignored, because the
    # wrong parse then fails outright and the walker fallback hides it --
    # which is exactly why the unanchored shape has to be here too.
    ("string-to-anchored",    "%f:string-to:XY%XYtail",     "hello worldXYtail"),
    ("string-to-open",        "%f:string-to:XY%%r:rest%",   "a bXYc"),
    ("string-to-first-match", "%f:string-to:XY%%r:rest%",   "aXYbXYc"),
    ("string-to-at-eol",      "%f:string-to:XY%XY",         "abcXY"),
    ("string-to-no-match",    "%f:string-to:XY%%r:rest%",   "abc"),
    ("string-to-at-offset-0", "%f:string-to:XY%%r:rest%",   "XYtail"),

    # date-rfc3164 also accepts the variant that carries a year.
    ("date-rfc3164-year",     "%f:date-rfc3164% z",         "Jun 26 2019 09:22:07 z"),

    # Alternatives inside a custom type: both engines must commit to the
    # same branch, and both must be able to back out of the first one.
    ("custom-type-quoted",    "A,%f:@cell%%r:rest%",        'A,"hello, world",tail',
     ['type=@cell:"%..:char-to:"%",', 'type=@cell:%..:char-sep:,%,']),
    ("custom-type-plain",     "A,%f:@cell%%r:rest%",        "A,plain,tail",
     ['type=@cell:"%..:char-to:"%",', 'type=@cell:%..:char-sep:,%,']),

    # Annotation values are not limited to what fits in an opcode.
    ("annot-long-value",      "A,%f:rest%",                 "A,x",
     ['annotate=t:+long="0123456789012345678901234567890123456789"'],
     "t"),
    # Same for a literal parser that stores its own matched text.
    ("named-literal-long",    '%{"name":"act","type":"literal",'
                              '"text":"WildFire Communications Status Changed"}%,z',
                              "WildFire Communications Status Changed,z"),
]


def run(turbo, rule, line, preamble=(), tags=""):
    with tempfile.NamedTemporaryFile("w", suffix=".rb", delete=False) as fh:
        fh.write("version=2\n")
        for extra in preamble:
            if not extra.startswith("annotate="):
                fh.write(extra + "\n")
        fh.write("rule=" + tags + ":" + rule + "\n")
        for extra in preamble:
            if extra.startswith("annotate="):
                fh.write(extra + "\n")
        rb = fh.name
    try:
        args = [LN, "-r", rb]
        if turbo:
            # strict: no silent walker fallback, see the note at the top
            args.insert(1, "-oturbostrict")
        p = subprocess.run(args, input=(line + "\n").encode(),
                           capture_output=True, timeout=15)
        out = p.stdout.decode(errors="replace").strip()
        try:
            return json.loads(out)
        except ValueError:
            return {"__unparseable__": out}
    finally:
        os.unlink(rb)


IGNORED_KEYS = {"originalmsg", "tags", "event.tags"}


def matched(j):
    return "unparsed-data" not in j and "__unparseable__" not in j


def strip(j):
    return {k: v for k, v in j.items() if k not in IGNORED_KEYS}


def main():
    fails = 0
    for case in CASES:
        name, rule, raw = case[0], case[1], case[2]
        preamble = case[3] if len(case) > 3 else ()
        tags = case[4] if len(case) > 4 else ""
        line = raw.encode().decode("unicode_escape")
        std = run(False, rule, line, preamble, tags)
        tb = run(True, rule, line, preamble, tags)
        sm, tm = matched(std), matched(tb)
        if sm != tm:
            status = "DIVERGE (match)"
        elif sm and tm and strip(std) != strip(tb):
            status = "DIVERGE (value)"
        else:
            status = "ok"
        if status != "ok" and name in KNOWN:
            status = "known-diff"
        if status not in ("ok", "known-diff"):
            fails += 1
            print("[%-15s] %s" % (status, name))
            print("    standard: %s" % json.dumps(strip(std)))
            print("    turbo   : %s" % json.dumps(strip(tb)))
        else:
            print("[%-15s] %s" % (status, name))
    print("\n%d divergence(s) out of %d field types" % (fails, len(CASES)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
