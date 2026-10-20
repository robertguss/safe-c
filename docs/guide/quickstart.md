# Quickstart

This page takes a clean machine to a small Safe C program that builds, lints
and runs. It uses only what `make install` puts under one prefix. Every command
and file below is run by `make guide-test`, so the page stays true.

## 1. Install the tools

Safe C builds with clang and lints with Semgrep and clang-tidy. The version
table is in [`AGENTS.md`](../../AGENTS.md#agent-rules) and the README
([Required tools](../../README.md#required-tools)). You do not need Frama-C to
adopt the library: it proves the library's own code, not yours. Without
Frama-C you still get the library, the headers, and the rule pack.

## 2. Install Safe C

From a checkout of this repository:

```sh install
make install PREFIX=$HOME/.local
export PKG_CONFIG_PATH=$HOME/.local/lib/pkgconfig
export PATH=$HOME/.local/bin:$PATH
```

That installs `libsafec.a`, the headers under `include/safec/`, `safec.pc`, and
`safec-lint`, the rule pack. `safec-lint` is the same lint `make lint` runs
here: Semgrep rules, clang-tidy, and the compiler warning set from
[`subset-v1.md`](../subset-v1.md).

## 3. Write a program

Application C cannot declare arrays or allocate, so the program's memory comes
from an arena, and the arena's backing bytes come from one small adapter file.
The adapter is outside the subset and outside lint (rule R0); it is the only
place the raw array lives.

```c file=adapters/storage.h
#ifndef APP_STORAGE_H
#define APP_STORAGE_H

#include <safec/arena.h>

/* Initializes *a over static backing storage. */
void app_arena_init(sc_arena *a);

#endif
```

```c file=adapters/storage.c
#include "storage.h"

static _Alignas(8) uint8_t backing[4096];

void app_arena_init(sc_arena *a) {
    sc_arena_init(a, backing, sizeof backing);
}
```

The program itself is inside the subset. It reads a request line, finds the
first space, and checks the method. Every read goes through `sc_slice_get`, and
every possibly missing value is an option you must test before you read it.

```c file=main.c
#include "adapters/storage.h"
#include <safec/result.h>
#include <safec/str.h>

/* The offset of the first space in s, or none. */
sc_option_size find_space(const sc_arena *a, sc_str s) {
    for (size_t i = 0; i < s.len; i++) {
        uint8_t b = 0;
        if (!sc_slice_get(a, s, i, &b)) {
            return sc_none_size();
        }
        if (b == ' ') {
            return sc_some_size(i);
        }
    }
    return sc_none_size();
}

int main(void) {
    sc_arena arena;
    app_arena_init(&arena);
    sc_str line = SC_STR_LIT(&arena, "GET /index HTTP/1.1");
    sc_option_size end = find_space(&arena, line);
    if (!end.some) {
        return 1;
    }
    sc_str method = sc_slice_sub(&arena, line, 0, end.value);
    return SC_STR_EQ_LIT(&arena, method, "GET") ? 0 : 1;
}
```

## 4. Build, lint and run

```sh project
cc -std=c11 -Wall -Wextra main.c adapters/storage.c $(pkg-config --cflags --libs safec) -o app
safec-lint
./app
```

`safec-lint` with no arguments scans every `.c` and `.h` file under the current
directory except `adapters/`, `tests/`, `fuzz/`, `docs/` and `build/`. A pass
prints `pass main.c`. Exit status 0 from `./app` means the method matched.

If your headers live in an `include/` directory, pass it through
`SAFEC_LINT_CPPFLAGS="-Iinclude"`.

## 5. Put the gates in CI

Call the reusable workflow
[`.github/workflows/safec-adopter.yml`](../../.github/workflows/safec-adopter.yml)
from your own repository:

```text
jobs:
  safec:
    uses: robertguss/safe-c/.github/workflows/safec-adopter.yml@main
    with:
      lint-paths: 'main.c'
      build-command: 'make test'
```

Its inputs are `lint-paths`, `lint-cppflags`, `build-command`,
`working-directory` and `safec-ref`; pin `safec-ref` to a tag or commit once
you depend on the result. The workflow installs Safe C, lints your paths, and
runs your build command with `safec` visible through pkg-config.
[`examples/adopter/`](../../examples/adopter/) is the sample the repository
runs it on.

## What to read next

- [Subset by example](subset-by-example.md) when lint rejects something.
- [Contracts](contracts.md) to read the library's guarantees, or to write a
  module of your own.
- [Red gates](red-gates.md) when a gate fails.
- [`docs/agent-prompt.md`](../agent-prompt.md) to hand an agent the rules.
