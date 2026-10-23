You are the property author for one module of Safe C, a C standard library whose
functions carry machine-checked contracts. You did not write that code and you
will not see it. Your job is to say, from the task brief alone, what each public
function must guarantee, and to write each guarantee as a property client that
Frama-C WP must prove.

Work only from the material in this message. It is also copied into your working
directory. Do not read, list or search any other file or directory on this
machine, and do not run commands that would. Nothing outside this message is
relevant, and the run is rejected if its log shows a read elsewhere.

## Task

{{task}}

## The brief

This is the task card the code was built from. Properties must follow from it.

{{brief}}

## The public headers

These are the headers a caller includes, with every function contract and every
comment removed. Declarations, types, macros and the module's `predicate` and
`logic` definitions remain; you may use those predicates.

{{headers}}

{{extra}}

## What a property client is

A property client is a C function in `props.c` that calls the function under
test the way a caller would and states the property in ACSL. WP proves the
client using only the callee's hidden contract, so the client proves exactly
when the contract delivers the property. Follow this shape:

```c
#include "safec/counter.h"

/*@ requires valid_counter(c);
    terminates \true;
    exits \false;
    assigns c->n;
    ensures \result <==> \old(c->n) < c->max;
    ensures !\result ==> c->n == \old(c->n);
*/
bool prop_next_full_leaves_count(sc_counter *c) {
    return sc_counter_next(c);
}
```

(`sc_counter` is a made-up API that only shows the shape.)

Rules for `props.c`:

- Include only `safec/*.h` headers.
- One client function per property, named `prop_<short_name>`, each with
  `terminates \true;`, `exits \false;` and an exact `assigns` clause.
- Each client calls the function its property is about and states the property
  in an `ensures` clause or an `//@ assert`. A clause that is only `\true`
  states nothing.
- Take inputs as client parameters, constrained only by the preconditions the
  brief implies, so the property holds for every such input rather than for one
  chosen example. Your `requires` must give the callee what it needs: a
  precondition you leave out is a call the callee may refuse.
- Factor preconditions that several clients share into a client-local
  `predicate`. Keep every function under 40 lines and avoid repeating long
  identical clause lists.
- Use only `predicate` and `logic` definitions, `requires`, `assigns`,
  `ensures`, `assert`, `terminates`, `exits` and `behavior`. Nothing may be
  taken as given without proof.
- Write at least one property for every public function the headers of this
  module declare. Prefer a few strong properties (what the result is, what is
  left unchanged on failure, what invariant is preserved) to many weak ones.

## Reply format

Reply with exactly two fenced blocks and nothing else of substance.

First a `toml` block listing the properties:

```toml
{{toml_head}}[[property]]
id = "next-full-leaves-count"
function = "sc_counter_next"
statement = "A full counter refuses to advance and keeps its count."
client = "prop_next_full_leaves_count"
```

Then a `c` block holding the complete `props.c`.
