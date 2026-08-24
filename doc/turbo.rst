TurboVM Bytecode Engine
=======================

TurboVM is an optional high-performance normalization engine for liblognorm.
It compiles rulebases into bytecode at startup and executes them through a
linear virtual machine with SIMD-accelerated parsing primitives. When enabled,
it provides significant throughput improvements over the default recursive
parser, especially on high-volume log streams.

Overview
--------

The default liblognorm normalization engine (the "recursive walker") traverses
the parse DAG node-by-node for each log message, allocating json-c objects as
fields are extracted. TurboVM replaces this with:

- **Bytecode compilation**: rulebases are compiled into a compact instruction
  sequence at load time. Each rule becomes a linear program.
- **Arena allocation**: all per-message memory comes from a single pre-allocated
  arena (~16 KB), fitting in L1 cache. Zero malloc/free per message.
- **SIMD parsing**: character scanning, delimiter search, whitespace skipping,
  and IP address parsing use SSE4.2 or NEON intrinsics when available.
- **Output parity**: field values match the standard engine, so a rulebase
  produces the same JSON on either engine (numeric fields are emitted as
  strings by default, as in the standard parser).
- **Nested JSON**: dotted field names (e.g. ``source.ip``) produce properly
  nested JSON objects (``{"source":{"ip":"..."}}``), enabling direct ECS
  (Elastic Common Schema) output.

Building with TurboVM
---------------------

TurboVM is an optional build feature, disabled by default::

    ./configure --enable-turbo

The build system automatically detects the CPU architecture and enables the
appropriate SIMD instruction set:

- **x86-64**: SSE4.2 (Intel Nehalem+, AMD Bulldozer+)
- **ARM64**: NEON (all ARMv8-A processors, including Apple M1/M2)
- **Other**: scalar fallback (functional but without SIMD acceleration)

No additional dependencies are required.

Using with lognormalizer
------------------------

The ``lognormalizer`` command-line tool supports turbo mode via the
``-oturbo`` option::

    $ lognormalizer -r rules.rb -e json -oturbo < messages.log

In turbo mode:

- Normalization uses the TurboVM bytecode engine
- Output is compact JSON with nested objects for dotted field names
- Field values match the standard engine (numeric fields are strings by
  default, or native JSON numbers with ``format="number"``)
- Tags are emitted under the literal key ``event.tags``, the same key the
  standard engine uses, and are always present (as with ``-T``)
- The ``getline()`` system call is used for input (more efficient than
  ``fgets()`` for large-scale processing)

If a rulebase cannot be compiled to bytecode (e.g. it uses unsupported
parser types), lognormalizer falls back to standard normalization
automatically. The same happens per message whenever the bytecode engine
declines one.

That fallback is what makes turbo safe to enable, and also what makes a
turbo defect hard to see: the standard engine quietly produces the right
answer, so comparing output tells you nothing about whether the bytecode
engine did any work. ``-oturbostrict`` turns the fallback off and reports
the failure instead::

    $ lognormalizer -r rules.rb -oturbostrict < messages.log

Use it when measuring turbo coverage or writing parity tests; a message
reported unparsed under ``-oturbostrict`` but parsed under ``-oturbo`` is
one the bytecode engine declined.

Library API
-----------

To enable TurboVM in your application, set the ``LN_CTXOPT_TURBO`` option
on the normalization context before loading rules::

    #include <liblognorm.h>

    ln_ctx ctx = ln_initCtx();
    ln_setCtxOpts(ctx, LN_CTXOPT_TURBO);
    ln_loadSamples(ctx, "/path/to/rules.rb");

After loading, verify that compilation succeeded::

    if (ln_turbo_is_available(ctx)) {
        /* TurboVM ready — ln_normalize() will use the fast path */
    }

For direct string output (bypassing json-c entirely)::

    char *json_str = NULL;
    size_t json_len = 0;
    int r = ln_normalize_to_str(ctx, msg, msg_len, &json_str, &json_len);
    if (r == 0 && json_str) {
        /* json_str contains the normalized JSON string */
        free(json_str);
    }

The standard ``ln_normalize()`` function also benefits from TurboVM
when it is enabled — the bytecode engine is used internally, with
automatic fallback to the recursive walker if needed.

High-performance API (lognorm-turbo.h)
--------------------------------------

Consumers that want to avoid json-c construction entirely (for example
rsyslog's ``mmnormalize`` worker hot path) can use the curated public
header ``lognorm-turbo.h``. It is installed alongside ``liblognorm.h``
and ``lognorm-features.h`` and gated on ``LOGNORM_TURBO_SUPPORTED``::

    #include <liblognorm/lognorm-features.h>
    #if defined(LOGNORM_TURBO_SUPPORTED)
    #include <liblognorm/lognorm-turbo.h>
    #endif

The header exposes only opaque types and a function-level contract; the
internal ``turbo*.h`` headers and the fast-result/snapshot struct layouts
are **not** installed and may change between releases without affecting the
ABI. The contract covers:

- ``ln_turbo_normalize_raw()`` — normalize into an opaque, context-owned
  result (valid until the next normalize call on that context);
- ``ln_turbo_snapshot_result()`` / ``ln_fast_result_snapshot_get()`` /
  ``ln_fast_result_snapshot_free()`` — retain a result beyond the next call;
- typed accessors ``ln_fast_result_field_count()``,
  ``ln_fast_result_get_field()``, ``ln_fast_result_get_field_typed()``
  (preserves ``LN_FTYPE_*`` value type and the ``LN_FFIELD_NESTED`` flag),
  ``ln_fast_result_get_string()`` / ``_get_int()``, the tag accessors and
  ``ln_fast_result_get_rule_id()``.

Supported Parsers
-----------------

TurboVM supports 32 of the 33 parser types defined in liblognorm v2.
The following parsers are compiled to bytecode:

- **Text**: ``word``, ``alpha``, ``string``, ``rest``, ``char-to``,
  ``char-separated``, ``string-to``, ``op-quoted-string``,
  ``quoted-string``, ``literal``
- **Numeric**: ``number``, ``float``, ``hexnumber``
- **Network**: ``ipv4``, ``ipv6``, ``mac48``
- **Date/Time**: ``date-rfc3164``, ``date-rfc5424``, ``date-iso``,
  ``time-24hr``, ``time-12hr``, ``duration``, ``kernel-timestamp``
- **Structured**: ``json``, ``cee-syslog``, ``cef``, ``v2-iptables``,
  ``checkpoint-lea``, ``name-value-list``
- **Special**: ``whitespace``, ``cisco-interface-spec``

The following parser type falls back to the standard engine:

- **repeat**: requires recursive sub-rule invocation, which is outside
  the scope of the single-pass VM instruction set.

The fallback is automatic and transparent: rulebases using ``repeat``
will still work correctly via the standard engine.

Known differences from the standard engine
-------------------------------------------

A few parsers behave slightly differently under TurboVM:

- ``cisco-interface-spec``, ``duration`` and ``kernel-timestamp`` are compiled
  as a plain word match, so they may accept input the standard parser rejects.
- ``char-to`` accepts a zero-length value where the standard parser requires
  at least one byte before the delimiter, so ``"",`` yields an empty string
  under TurboVM and the literal ``""`` under the standard parser.
- ``string-to`` with a single-character delimiter never matches. That is the
  standard parser's behaviour (its inner comparison loop cannot run for a
  delimiter shorter than two bytes) and TurboVM replicates it deliberately
  so a rulebase behaves the same on both engines.
- ``json`` field names follow the same three contracts as the standard
  parser (see configuration.rst "Special field names"): ``-`` is matched
  and discarded; ``.`` inlines object keys at the current context; a
  real name stores the value as one nested JSON object (verbatim bytes,
  so arrays / booleans / nulls round-trip). The ``.`` inline path walks
  the object into the turbo arena (dotted leaves, 64-leaf cap). Nested
  arrays on that path become indexed keys (``.0``, ``.1``) rather than
  JSON arrays. Named ``%field:json%`` keeps arrays as arrays.

Performance Notes
-----------------

Throughput improvements depend on the rulebase complexity and message
format. Typical observations:

- Simple rulebases (5-10 rules): 2-3x throughput improvement
- Complex rulebases (50+ rules with alternatives): 5-10x improvement
- The ``ln_normalize_to_str()`` path avoids json-c entirely and provides
  the highest throughput for applications that consume JSON as strings

TurboVM adds no overhead when disabled (``--disable-turbo`` or default).
Compilation is all-or-nothing per rulebase: the compiler walks one shared
parse DAG, so a construct it cannot express means the whole rulebase runs
on the recursive walker. When that happens the partially emitted program is
discarded, ``ln_turbo_is_available()`` reports false, and no per-message
work is wasted.

Limits
------

Two fixed-size structures bound what a single message can carry:

- ``LN_FAST_MAX_FIELDS`` (128) fields per result. Rulebases over CSV-shaped
  sources reach this: a PAN-OS TRAFFIC record is 97 columns before
  annotations. Exceeding it is not silent — ``ln_normalize_to_str()``
  refuses the result and falls back to the recursive walker, which has no
  field limit. Callers of ``ln_turbo_normalize_raw()`` get the partial
  result plus ``ln_fast_result_is_truncated()`` and decide for themselves.
- ``LN_FAST_MAX_TAGS`` (16) tags per result, with the same contract.

A third limit is on the rulebase rather than the message: the compiler
recurses once per parser and once per literal in a rule and gives up past a
depth of 200, so a rule runs out of compile depth at roughly 100 sequential
fields. Compilation is all-or-nothing, so one rule that long puts the whole
rulebase on the recursive walker. ``-oturbostrict`` and the
``turbo: compilation failed`` debug line are the way to notice.

Field names, annotation values and literal texts are *not* limited by the
size of the opcode buffers they normally live in: anything that does not fit
inline is interned in the program string pool.
