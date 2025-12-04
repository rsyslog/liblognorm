PDAG engine notes
=================

The following notes summarize the implementation-relevant insights from
Rainer Gerhards' master's thesis, *Efficient normalization of IT log messages
under realtime conditions* (2016), which introduced liblognorm v2's
PDAG-based normalizer. The full thesis is available from the author's site:
`PDF download <https://rainer.gerhards.net/files/download/realtime_log_normalization_rainer_gerhards.pdf>`_.

These notes are meant as a quick reference for developers and AI-assisted
maintenance work.

Terminology
-----------

- **log message** ``l``: input string/byte sequence to parse
- **suffix** ``s``: the not-yet-consumed suffix of ``l`` during parsing
- **motif**: token/parser that matches a prefix of ``s`` and optionally extracts a value
- **rulebase**: set of rules, each rule is a sequence of motifs and literals defining a message format
- **terminal node**: node that represents the end of at least one rule
- **component**: disconnected subgraph used to model a named user-defined motif
- **parsing**: combined operation of finding a matching rule and extracting fields during that walk

Constraint: **motifs may not match the empty string**; each successful motif must
consume at least one byte.

Motivation for the PDAG design
------------------------------

The PDAG approach addresses challenges observed in the v1 proof-of-concept:

- high per-node memory consumption from literal lookup tables
- ambiguous matches that require controllable priority
- speed and cache friendliness
- richer built-in motif set and user extensibility
- support for mixed message formats in one normalizer
- runtime characteristics that can be analyzed

Implementation deltas observed in liblognorm
--------------------------------------------
The current codebase mostly follows the thesis guidance but there are some
notable deviations:

- **Empty-match motif still exists**: the ``rest`` parser succeeds even when
  invoked at the end of the input (it consumes zero bytes), violating the
  thesis invariant that every motif must consume at least one byte on
  success.【F:src/parser.c†L1570-L1590】

- **Literals are not expanded per character during loading**: rule loading
  adds complete literal parsers directly, rather than first expanding them
  into single-character motifs and recombining later. Literal path compaction
  is only attempted when a literal node has a single predecessor and child,
  which skips compression on shared DAG segments (``refcnt`` must be ``1``),
  leaving many multi-character literals uncompressed when suffix sharing is
  present.【F:src/pdag.c†L332-L360】

- **Literal compression depends on parser metadata**: compaction refuses to
  merge literals that carry a field name or terminate a rule, whereas the
  thesis model compresses pure literal stretches regardless of adjacent rule
  boundaries. This keeps more nodes intact than the thesis’s “compress all
  literal paths” expectation.【F:src/pdag.c†L332-L356】

PDAG data model
---------------

- The rulebase is represented as a **rooted DAG** with one designated root
  component.
- A PDAG can have **multiple disconnected components**; each component has one
  root node and models a named user-defined motif.
- **Literals are handled as motifs** so parsing logic can evaluate all edges in a
  uniform way and in **priority order**.
- Looping constructs belong **inside motif parsers**, keeping the PDAG itself acyclic.

Construction workflow
---------------------

**Load phase** (build first, optimize later)

1. Select the current component (root component by default; switch if a rule
   specifies another component).
2. Split the rule into a sequence of motifs ``M``.
3. Expand literal strings into per-character literal motifs during load.
4. For each motif, create the edge and destination node if it does not already
   exist and advance; mark the final node as terminal.

**Preparation/optimization phase**

1. Establish the **motif priority order** for each node's outgoing edges.
2. Apply **literal path compression** where nodes have one incoming and one
   outgoing literal edge, collapsing runs of literals into a single edge.

Parsing algorithm
-----------------

Given a node ``n`` and suffix ``s``:

1. If ``s`` is empty: succeed only if ``n`` is terminal.
2. Otherwise, iterate over ``n``'s outgoing edges **in priority order**:
   - If an edge matches a prefix of ``s``, recurse into its destination with the
     remaining suffix. Extraction happens only if the recursive call succeeds.
   - On failure, discard any partial extraction from that attempt and continue
     with the next edge.
3. If no edge succeeds, parsing fails.

Disconnected components act like motifs by recursively invoking parsing on their
own root and succeeding once a terminal node in that component is reached,
without requiring all of ``s`` to be consumed.

Ambiguity control
-----------------

Motif ambiguity is handled by **prioritized edge evaluation**; deterministic
outcomes rely on traversing edges in that configured order.

Performance considerations
--------------------------

- Keep the PDAG **read-only after construction**; store mutable state in the
  message object or on the stack for better cache behavior and thread safety.
- Use **small indexes instead of function pointers** for motif parser
  identifiers to shrink edge structures.
- Favor **narrow integer types and bitfields** where safe to reduce memory
  footprint.

Complexity expectations
-----------------------

- Practical behavior is typically close to **O(|l|)** because motifs often
  consume multiple bytes and nodes have few outgoing edges.
- Theoretical worst case with backtracking is exponential in ``|l|``. Without
  backtracking, an adversarial setup can reach **O(|l|^2)** if many costly motif
  checks occur at each node.
- Expected practical worst case with limited backtracking depth ``v`` is roughly
  **O(|l|^(2+v))**.
