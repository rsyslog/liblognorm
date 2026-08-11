#!/usr/bin/env python3
# TurboVM vs standard-parser field-type parity audit.
#
# This file is part of the liblognorm project, released under ASL 2.0.
#
# For each (rule, input) it runs lognormalizer twice, once with the standard
# parser and once with -oturbo, and compares the parsed JSON semantically (so
# key order and whitespace do not matter). It exits non-zero if any field type
# produces different output on the two engines, except for the types listed in
# KNOWN, which TurboVM intentionally approximates.
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
LN = os.environ.get("LN", os.path.join(BUILD, "src", ".libs", "lognormalizer"))
LIBDIR = os.environ.get("LN_LIBDIR", os.path.join(BUILD, "src", ".libs"))
if os.path.isdir(LIBDIR):
    env_lib = os.environ.get("LD_LIBRARY_PATH", "")
    os.environ["LD_LIBRARY_PATH"] = LIBDIR + (":" + env_lib if env_lib else "")

# Field types TurboVM approximates as a plain word match, so they can accept or
# format differently from the standard parser. Documented, not a regression.
KNOWN = {"cisco-interface-spec", "duration", "kernel-timestamp"}

# (name, rule-body-after-':', input-line). Use \t / \n escapes in the input.
CASES = [
    ("word",                  "%f:word% z",                 "hello z"),
    ("alpha",                 "%f:alpha%123",               "abc123"),
    ("number",                "%f:number% z",               "12345 z"),
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
    ("cee-syslog",            "%f:cee-syslog%",             '@cee: {"a":1}'),
    ("name-value-list",       "%f:name-value-list%",        "a=1 b=2"),
    ("cef",                   "%f:cef%",                    "CEF:0|V|P|1.0|100|n|5|src=1.2.3.4"),
    ("cisco-interface-spec",  "%f:cisco-interface-spec% z", "GigabitEthernet0/1 z"),
    ("iptables",              "%f:iptables%",               "SRC=1.2.3.4 DST=5.6.7.8 LEN=40"),
    ("string",                "%f:string% z",               "value z"),
    ("tokenized",             "%f:tokenized: :number%",     "1 2 3"),
    ("checkpoint-lea",        "%f:checkpoint-lea%",         "a=1; b=2;"),
]


def run(turbo, rule, line):
    with tempfile.NamedTemporaryFile("w", suffix=".rb", delete=False) as fh:
        fh.write("version=2\nrule=:" + rule + "\n")
        rb = fh.name
    try:
        args = [LN, "-r", rb]
        if turbo:
            args.insert(1, "-oturbo")
        p = subprocess.run(args, input=(line + "\n").encode(),
                           capture_output=True, timeout=15)
        out = p.stdout.decode(errors="replace").strip()
        try:
            return json.loads(out)
        except ValueError:
            return {"__unparseable__": out}
    finally:
        os.unlink(rb)


def matched(j):
    return "unparsed-data" not in j and "__unparseable__" not in j


def strip(j):
    return {k: v for k, v in j.items() if k != "originalmsg"}


def main():
    fails = 0
    for name, rule, raw in CASES:
        line = raw.encode().decode("unicode_escape")
        std, tb = run(False, rule, line), run(True, rule, line)
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
