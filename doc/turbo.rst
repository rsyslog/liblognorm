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
- **Typed field output**: extracted fields carry their native type (string,
  integer, double, boolean) instead of converting everything to JSON strings.
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
- Numeric fields are emitted as native JSON numbers (not strings)
- The ``getline()`` system call is used for input (more efficient than
  ``fgets()`` for large-scale processing)

If a rulebase cannot be compiled to bytecode (e.g. it uses unsupported
parser types), lognormalizer falls back to standard normalization
automatically.

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
- **Special**: ``whitespace`` (as skip), ``cisco-interface-spec``

The following parser type falls back to the legacy recursive engine:

- **repeat**: requires recursive sub-rule invocation, which is outside
  the scope of the single-pass VM instruction set.

The fallback is automatic and transparent — rulebases using ``repeat``
will still work correctly via the standard engine.

Performance Notes
-----------------

Throughput improvements depend on the rulebase complexity and message
format. Typical observations:

- Simple rulebases (5-10 rules): 2-3x throughput improvement
- Complex rulebases (50+ rules with alternatives): 5-10x improvement
- The ``ln_normalize_to_str()`` path avoids json-c entirely and provides
  the highest throughput for applications that consume JSON as strings

TurboVM adds no overhead when disabled (``--disable-turbo`` or default).
When enabled but compilation fails for a specific rule, only that rule
falls back to the recursive walker — other rules still use bytecode.
