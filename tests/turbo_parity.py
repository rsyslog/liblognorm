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

# date-iso uses OP_FIELD_DATE aux=1 (YYYY-MM-DD). RFC3164/5424 stay on
# ln_simd_timestamp (aux=0). Alpha uses OP_FIELD_WORD aux=2. Hexdigit-lazy
# string uses aux=1. Duration is OP_FIELD_TIME aux=2. The SIMD whitespace
# word stays on aux=0.
KNOWN = set()

# (name, rule-body-after-':', input-line[, extra rulebase lines]).
# Use \t / \n escapes in the input.
CASES = [
    ("word",                  "%f:word% z",                 "hello z"),
    ("alpha",                 "%f:alpha%123",               "abc123"),
    # NBSP is not a letter. A whitespace word swallowed it and the rest of
    # the rule never saw the next token.
    ("alpha-nbsp",            "%f:alpha%%r:rest%",          "alice\\xa0Host"),
    ("number",                "%f:number% z",               "12345 z"),
    # Two parsers, one name: libfastjson keeps both keys, newest first, so a
    # RFC parser that last-wins on duplicates sees the first-added value.
    # Live lookup (get_ex) is last-wins. RFC5424 <pri>ver.
    ("dup-name-rfc5424-pri",  "%.:@pri% %h:word%",          "<15>1 host.example",
     ["type=@pri:<%priority:number%>%priority:number%",
      "type=@pri:%priority:number%"]),
    ("dup-name-seq",          "<%a:number%>%a:number% z",   "<15>1 z"),
    # Walker Number is [0-9]+. A leading sign is not a number, so a later
    # rule can match.
    ("number-leading-minus",  "%f:number%",                 "-3"),
    ("number-leading-plus",   "%f:number%",                 "+3"),
    ("number-leading-minus-fallback", "%r:rest%",           "-3 x",
     ["rule=:%n:number% x"]),
    # The VM used to skip leading whitespace before the first instruction.
    # The walker never does, so a leading space is either kept (char-to, rest)
    # or it makes the first field refuse (word, number).
    ("word-lead-space",       "%f:word%",                   " abc"),
    ("number-lead-space",     "%f:number%",                 " 123"),
    ("char-to-lead-space",    "%f:char-to:,%%r:rest%",      " a,b"),
    ("rest-lead-space",       "%f:rest%",                   " abc"),
    ("number-fmt",            '%{"name":"f","type":"number","format":"number"}% z', "42 z"),
    ("hexnumber-fmt",         '%{"name":"f","type":"hexnumber","format":"number"}% z', "0xff z"),
    ("float-fmt",             '%{"name":"f","type":"float","format":"number"}% z', "3.14159 z"),
    ("float",                 "%f:float% z",                "3.14 z"),
    ("hexnumber",             "%f:hexnumber% z",            "0xff z"),
    ("char-to",               "%f:char-to:,%,z",            "abc,z"),
    ("char-sep",              "%f:char-sep:,%,z",           "abc,z"),
    # Empty extra: walker has no terminator, so the field is the rest,
    # including spaces. Defaulting the delimiter to space stopped early.
    ("char-sep-empty-extra",  "%f:char-sep:%",              "aa bb cc"),
    ("rest",                  "%f:rest%",                   "everything after"),
    ("quoted-string",         "%f:quoted-string%",          '"hello world"'),
    ("op-quoted-string",      "%f:op-quoted-string%",       "plainword"),
    ("op-quoted-quoted",      "%f:op-quoted-string% z",     '"hello world" z'),
    # Default op-quoted does not treat backslash as escape. Eating the
    # closing quote after \\ made Windows paths refuse the field.
    ("op-quoted-winpath",     "%f:op-quoted-string% z",     '"C:\\\\dir\\\\file" z'),
    ("op-quoted-trail-bs",    "%f:op-quoted-string% z",     '"C:\\\\foo\\\\" z'),
    ("whitespace",            "start%f:whitespace%ok",      "start \\t ok"),
    ("ipv4",                  "%f:ipv4% z",                 "192.0.2.1 z"),
    ("ipv6",                  "%f:ipv6% z",                 "2001:db8::1 z"),
    # Leading "::" is valid (RFC4291). The VM used to require a hex nibble
    # before the first colon, so ::1 and IPv4-mapped ::ffff:a.b.c.d refused.
    ("ipv6-lead-compress",    "%f:ipv6% z",                 "::1 z"),
    ("ipv6-lead-compress-all","%f:ipv6% z",                 ":: z"),
    ("ipv6-mapped-v4",        "%f:ipv6% z",                 "::ffff:192.0.2.1 z"),
    # Walker accepts a single hex nibble when two bytes remain ("c" in
    # "client..."). Requiring two consumed bytes made @ip fail and a later
    # hostname alternative match.
    ("ipv6-one-nibble",       "%f:ipv6%%r:rest%",           "client.example.test"),
    ("ipv6-one-nibble-eos",   "%f:ipv6%",                   "c"),
    # Same address through the @ip custom type (ipv4 then ipv6), the shape
    # a later literal "]" needs.
    ("ip-custom-mapped-v4",   "[%source_ip:@ip%]",          "[::ffff:192.0.2.1]",
     ["type=@ip:%..:ipv4%", "type=@ip:%..:ipv6%"]),
    ("mac48",                 "%f:mac48% z",                "00:11:22:33:44:55 z"),
    ("date-rfc3164",          "%f:date-rfc3164% z",         "Aug 10 12:00:00 z"),
    ("date-rfc5424",          "%f:date-rfc5424% z",         "2026-08-10T12:00:00Z z"),
    ("date-iso",              "%f:date-iso% z",             "2026-08-10 z"),
    ("date-iso-bad-month",    "%f:date-iso%%r:rest%",       "2026-13-10 rest"),
    ("date-iso-then-time",    "%d:date-iso% %t:time-24hr%", "2024-01-15 13:45:00"),
    ("time-24hr",             "%f:time-24hr% z",            "13:45:00 z"),
    ("time-12hr",             "%f:time-12hr% z",            "11:45:00 z"),
    ("time-12hr-bad-hour",    "%f:time-12hr%%r:rest%",      "13:45:00 rest"),
    ("duration",              "%f:duration% z",             "01:30:00 z"),
    ("duration-one-digit",    "%f:duration% z",             "9:30:00 z"),
    ("duration-long-hour",    "%f:duration% z",             "25:00:00 z"),
    ("kernel-timestamp",      "%f:kernel-timestamp% z",     "[12345.123456] z"),
    ("kernel-timestamp-long", "%f:kernel-timestamp% z",     "[1234567890.123456] z"),
    ("kernel-timestamp-fallback", "%r:rest%",               "not-a-kts z",
     ["rule=:%f:kernel-timestamp% z"]),
    ("json",                  "%f:json%",                   '{"a":1,"b":[1,2]}'),
    # json-c consumes whitespace after the value. Stopping at '}' left a
    # leading space for the next literal and the rule failed.
    ("json-trail-space",      "%f:json%end",                '{"a":1} end'),
    # libfastjson, which the walker uses, accepts single-quoted keys and
    # strings. Strict RFC JSON left those values unmatched.
    ("json-single-quote",     "%f:json%end",                "{'a':1}end"),
    ("json-single-quote-space","%f:json%end",               "{'a':1} end"),
    ("json-single-quote-str", "%f:json%",                   "{'a':'b'}"),
    ("json-true-cap",         "%f:json%",                   "{'a':True}"),
    ("json-false-cap",        "%f:json%",                   "{'a':False}"),

    # %.:json% merges the parsed object into the parent rather than storing it
    # under a name. Arrays inside it must stay arrays: flattening them to .0/.1
    # keys is indistinguishable from an object with numeric keys.
    ("json-merge",            "%.:json%",                   '{"a":1}'),
    ("json-merge-array",      "%.:json%",                   '{"c":["x","y"]}'),
    ("json-merge-array-obj",  "%.:json%",       '{"a":{"b":[{"n":"0"},{"n":"1"}]}}'),
    ("json-merge-array-deep", "%.:json%",       '{"a":{"b":{"c":[1,[2,3]]}}}'),
    # Empty nested objects are stored. Turbo used to skip them because the
    # flatten walk emits leaves, and an object with no members produced none.
    ("json-merge-empty-obj",  "%.:json%",                   '{"dns":{"grouped":{}}}'),
    ("json-merge-empty-sib",  "%.:json%",                   '{"a":1,"b":{}}'),
    ("json-merge-empty-arr",  "%.:json%",                   '{"b":[]}'),

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
    # Walker json_object_object_add replaces; a second pair wins.
    ("name-value-list-dup",
     '%{"name":"log","type":"name-value-list","separator":" ","assignator":"="}%',
     "src=192.0.2.1 msg=one src=192.0.2.9"),

    # A key parsed out of the message, longer than any inline buffer: turbo
    # arena-allocates the name, so it must not be clipped.
    ("name-value-list-long-key",
     "%f:name-value-list%",
     "kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk=1 b=2"),
    # Custom assignator: names are not restricted to alnum. A quote in the
    # extracted key must be JSON-escaped; memcpy of the raw name is invalid.
    ("name-value-list-quote-key",
     '%{"name":"data","type":"name-value-list","separator":",","assignator":"=","ignore_whitespaces":true}%',
     'alice "/Common/lab=1'),
    ("cef",                   "%f:cef%",                    "CEF:0|V|P|1.0|100|n|5|src=1.2.3.4"),
    ("cef-dup-src",           "%f:cef%",
     "CEF:0|V|P|1.0|100|n|5|src=192.0.2.1 msg=hello src=192.0.2.9"),
    # A trailing unescaped '=' after an escaped backslash is a pair delimiter.
    ("cef-trail-eq",          "%f:cef%",                    "CEF:0|V|P|1.0|100|n|5|data=abc\\\\="),
    ("cef-trail-eq-next",     "%f:cef%",                    "CEF:0|V|P|1.0|100|n|5|data=abc\\\\= next=1"),
    ("cef-one-sp",            "%f:cef%",                    "CEF:0|V|P|1.0|100|n|5|msg=hello src=1.2.3.4"),
    ("cef-two-sp",            "%f:cef%",                    "CEF:0|V|P|1.0|100|n|5|msg=hello  src=1.2.3.4"),
    # Extension names are alnum, '_' or '.'. A hyphen refuses the parser so a
    # later rule can match. Accepting "bar-baz" left HTML query-string keys
    # in the document and broke the JSON.
    ("cef-hyphen-key",        "%f:cef%",                    "CEF:0|V|P|1.0|100|n|5|foo=a bar-baz=1"),
    ("cef-hyphen-key-fallback","%r:rest%",                  "CEF:0|V|P|1.0|100|n|5|foo=a bar-baz=1",
     ["rule=:%.:cef%"]),
    ("cef-underscore-key",    "%f:cef%",                    "CEF:0|V|P|1.0|100|n|5|cs1_Label=ok next=1"),
    ("cisco-interface-spec",  "%f:cisco-interface-spec% z",
     "outside:192.0.2.1/50349 z"),
    ("cisco-interface-spec-ip-only", "%f:cisco-interface-spec% z",
     "192.0.2.1/50179 z"),
    ("cisco-interface-spec-user", "%f:cisco-interface-spec% z",
     "outside:192.0.2.1/50349(alice) z"),
    ("cisco-interface-spec-ip2", "%f:cisco-interface-spec% z",
     "outside:192.0.2.1/50179 (192.0.2.2/50180) z"),
    ("cisco-interface-spec-full", "%f:cisco-interface-spec% z",
     "outside:192.0.2.1/50179 (192.0.2.2/50180) (alice) z"),
    ("cisco-interface-spec-fallback", "%r:rest%", "notaniface z",
     ["rule=:%f:cisco-interface-spec% z"]),
    ("iptables",              "%f:iptables%",               "SRC=1.2.3.4 DST=5.6.7.8 LEN=40"),
    ("v2-iptables",           "%f:v2-iptables%",            "SRC=192.0.2.1 DST=192.0.2.2 LEN=40"),
    ("v2-iptables-dup-len",   "%f:v2-iptables%",
     "SRC=192.0.2.1 DST=192.0.2.2 LEN=40 LEN=52"),
    ("v2-iptables-flag",      "%f:v2-iptables%",            "SRC=192.0.2.1 DST=192.0.2.2 DF"),
    ("checkpoint-lea",        "%f:checkpoint-lea%",         "src: 192.0.2.1; dst: 192.0.2.2;"),
    ("checkpoint-lea-dup",    "%f:checkpoint-lea%",
     "layer_name: one; layer_name: two;"),
    ("string",                "%f:string% z",               "value z"),
    # string{hexdigit,lazy} stops at the first non-hex byte. Compiling it as a
    # whitespace word ate "01250003:5:" as one field and the rule failed.
    ("string-hexdigit-lazy",
     '%{"name":"f","type":"string","matching.permitted":[{"class":"hexdigit"}],'
     '"matching.mode":"lazy"}%:%n:number%',
     "01250003:5"),
    ("tokenized",             "%f:tokenized: :number%",     "1 2 3"),
    ("checkpoint-lea",        "%f:checkpoint-lea%",         "a=1; b=2;"),
    # Quoted LEA values drop the quotes. A semicolon inside them belongs to
    # the value; ending the field at the first ';' left text behind and the
    # rule failed, or kept the quotes on a message that still matched.
    ("checkpoint-lea-quoted",
     '[ %{"name":"f","type":"checkpoint-lea","terminator":"]"}%]',
     '[ k:"v"; ]'),
    ("checkpoint-lea-quoted-no-semi",
     '[ %{"name":"f","type":"checkpoint-lea","terminator":"]"}%]',
     '[ k:"v" ]'),
    ("checkpoint-lea-semi-in-quotes",
     '[ %{"name":"f","type":"checkpoint-lea","terminator":"]"}%]',
     '[ desc:"a;b"; k:"c" ]'),

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
    # Walker ordered-choice: a successful @ip alternative is not retried as
    # hostname when the rest of the rule fails. "client..." yields ipv6 "c".
    ("custom-type-commit",    "X %f:@x% attempted",        "X client attempted",
     ["type=@x:%..:ipv6%", "type=@x:%..:word%"]),
    ("custom-type-commit-fallback", "%r:rest%",            "X client attempted",
     ["type=@x:%..:ipv6%", "type=@x:%..:word%",
      "rule=:X %f:@x% attempted"]),
    # Header already bound timestamp; a later custom type of the same name
    # must not replace the scalar with nested date/time children.
    ("ctx-parent-first-wins",
     "%timestamp:date-rfc3164% %timestamp:@bts% z",
     "Jan 15 10:30:45 2024-01-15 10:30:45 z",
     ["type=@bts:%date:date-iso% %time:time-24hr%"]),
    ("ctx-parent-free",
     "%timestamp:@bts% z",
     "2024-01-15 10:30:45 z",
     ["type=@bts:%date:date-iso% %time:time-24hr%"]),

    # Annotation values are not limited to what fits in an opcode.
    ("annot-long-value",      "A,%f:rest%",                 "A,x",
     ['annotate=t:+long="0123456789012345678901234567890123456789"'],
     "t"),
    # Same for a literal parser that stores its own matched text.
    ("named-literal-long",    '%{"name":"act","type":"literal",'
                              '"text":"WildFire Communications Status Changed"}%,z',
                              "WildFire Communications Status Changed,z"),
    # Unnamed literal past the 60-byte inline opcode buffer. pdagOptimize
    # concatenates the run; refusing it used to abort the whole rulebase.
    ("literal-long-unnamed",
     "the_quick_brown_fox_jumps_over_the_lazy_dog_and_then_some_more %f:word%",
     "the_quick_brown_fox_jumps_over_the_lazy_dog_and_then_some_more hello"),

    # repeat: parser-sub loops while while-sub matches. The result is a JSON
    # array, or the "." inner name unwraps to an array of scalars.
    ("repeat-numbers",
     'a %{"name":"numbers","type":"repeat","parser":{"name":"n","type":"number"},'
     '"while":{"type":"literal","text":", "}}% b %w:word%',
     "a 1, 2, 3, 4 b test"),
    ("repeat-pairs",
     'a %{"name":"numbers","type":"repeat","parser":['
     '{"name":"n1","type":"number"},{"type":"literal","text":":"},'
     '{"name":"n2","type":"number"}],'
     '"while":{"type":"literal","text":", "}}% b %w:word%',
     "a 1:2, 3:4, 5:6, 7:8 b test"),
    ("repeat-dot-word",
     'flags %{"name":"flags","type":"repeat",'
     '"parser":{"type":"word","name":"."},'
     '"while":{"type":"literal","text":" "}}% x',
     "flags RST ACK x"),
    ("repeat-permit-mismatch",
     'flags %{"name":"flags","type":"repeat",'
     '"option.permitMismatchInParser":true,'
     '"parser":{"type":"word","name":"."},'
     '"while":{"type":"literal","text":" "}}% on',
     "flags RST  on"),
    # Without that option a later parser miss fails the whole repeat, so a
    # later rule can match. Keeping the collected array was a turbo-only hit.
    ("repeat-parser-miss",
     "%r:rest%",
     "flags RST  on",
     ['rule=:flags %{"name":"flags","type":"repeat",'
      '"parser":{"type":"word","name":"."},'
      '"while":{"type":"literal","text":" "}}% on']),
    ("repeat-fail-dup",
     'a %{"name":"numbers","type":"repeat","option.failOnDuplicate":true,'
     '"parser":[{"name":"n","type":"number"},{"type":"literal","text":":"},'
     '{"name":"n","type":"number"}],'
     '"while":{"type":"literal","text":", "}}% b',
     "a 1:2 b"),
    # Invoked under "-": match and consume, do not store the array.
    ("repeat-discard",
     '%-:@g%%r:rest%',
     "a, b, c rest",
     ['type=@g:%groups:repeat{"parser":{"type":"word","name":"."},'
      '"while":{"type":"literal","text":", "}}%']),
    ("repeat-while-alt",
     'a %{"name":"numbers","type":"repeat","parser":['
     '{"name":"n1","type":"number"},{"type":"literal","text":":"},'
     '{"name":"n2","type":"number"}],'
     '"while":{"type":"alternative","parser":['
     '{"type":"literal","text":", "},{"type":"literal","text":","}]}}% b %w:word%',
     "a 1:2, 3:4,5:6, 7:8 b test"),
    # Named repeat whose body is json. vm_pack_iter must nest the object, not
    # quote the captured span. Under 48 bytes is STRING_INLINE; over it is the
    # pointer path. A body that is not JSON fails both engines; tokener
    # fallback on a stored RAW_JSON span is covered by turbo_test_compile.
    ("repeat-json",
     '%{"name":"items","type":"repeat","parser":{"name":"j","type":"json"},'
     '"while":{"type":"literal","text":","}}%',
     '{"a":1},{"b":2}'),
    ("repeat-json-long",
     '%{"name":"items","type":"repeat","parser":{"name":"j","type":"json"},'
     '"while":{"type":"literal","text":","}}%',
     '{"k":"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"},'
     '{"k":"yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy"}'),
    ("repeat-json-bad",
     '%{"name":"items","type":"repeat","parser":{"name":"j","type":"json"},'
     '"while":{"type":"literal","text":","}}%',
     '{,},{"a":1}'),
]

# Every v2 parser in parser_lookup_table must appear in CASES. tokenized is
# v1-only. alternative is a pdag combinator, covered via repeat-while-alt.
V2_PARSERS = [
    "literal", "repeat", "date-rfc3164", "date-rfc5424", "number", "float",
    "hexnumber", "kernel-timestamp", "whitespace", "ipv4", "ipv6", "word",
    "alpha", "rest", "op-quoted-string", "quoted-string", "date-iso",
    "time-24hr", "time-12hr", "duration", "cisco-interface-spec", "json",
    "cee-syslog", "mac48", "cef", "v2-iptables", "name-value-list",
    "checkpoint-lea", "string-to", "char-to", "char-sep", "string",
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


def parsers_used():
    import re
    used = set()
    for case in CASES:
        chunks = [case[1]]
        if len(case) > 3:
            chunks.extend(case[3])
        text = "\n".join(chunks)
        for m in re.finditer(r':([a-z0-9-]+)(?:%|:|,|")', text):
            used.add(m.group(1))
        for m in re.finditer(r'"type"\s*:\s*"([a-z0-9-]+)"', text):
            used.add(m.group(1))
        if "type=literal" in text or '"type":"literal"' in text:
            used.add("literal")
        if "%{" in text and "literal" in text:
            used.add("literal")
    # named-literal-long uses type literal
    used.add("literal")
    return used


def main():
    fails = 0
    missing = [p for p in V2_PARSERS if p not in parsers_used()]
    extra_needed = []
    if missing:
        print("MISSING PARSER COVERAGE: %s" % ", ".join(missing))
        fails += 1
        extra_needed = missing
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
