#!/usr/bin/env python3
"""Require that GenMC (scripts/genmc.py) rejects planted bugs in stdlib/chan.c and stdlib/mailbox.c.

Each mutant is one edit; GenMC must report an assertion failure or a race in some
configuration. Three edits are left out on purpose. Relaxing sc_mbox_close's
fetch_or or recv's tail load changes nothing: close is the receiver's own action
and a sender that sees the closed bit touches no shared data, so those orderings
are stronger than needed. Dropping claim's FULL exit turns FULL into a spin,
which is a liveness bug that GenMC's safety mode prunes by design."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests"))
from planted import require_red, tool_env

LOGS = ROOT / "build/genmc/mutants"
ENV = tool_env("GENMC", ROOT / "build/tools/genmc/bin/genmc")
DIRS = ("scripts", "include", "stdlib", "tests/genmc", "tests/side.h")

CHAN = [
    ("tail-relaxed", "atomic_store_explicit(&c->tail, next, memory_order_release)",
     "atomic_store_explicit(&c->tail, next, memory_order_relaxed)"),
    ("head-relaxed", "head + 1 == c->entries ? 0 : head + 1, memory_order_release",
     "head + 1 == c->entries ? 0 : head + 1, memory_order_relaxed"),
    ("recv-tail-relaxed", "if (head == atomic_load_explicit(&c->tail, memory_order_acquire)) {",
     "if (head == atomic_load_explicit(&c->tail, memory_order_relaxed)) {"),
    ("send-head-relaxed", "if (next == atomic_load_explicit(&c->head, memory_order_acquire))",
     "if (next == atomic_load_explicit(&c->head, memory_order_relaxed))"),
    ("close-relaxed", "atomic_store_explicit(&c->closed, true, memory_order_release)",
     "atomic_store_explicit(&c->closed, true, memory_order_relaxed)"),
    ("recv-closed-relaxed", "if (!atomic_load_explicit(&c->closed, memory_order_acquire))",
     "if (!atomic_load_explicit(&c->closed, memory_order_relaxed))"),
    ("no-tail-recheck",
     "if (head == atomic_load_explicit(&c->tail, memory_order_acquire))\n            return SC_CHAN_CLOSED;",
     "return SC_CHAN_CLOSED;"),
    ("payload-after-publish",
     "    c->ring[tail] = value;\n    atomic_store_explicit(&c->tail, next, memory_order_release);",
     "    atomic_store_explicit(&c->tail, next, memory_order_release);\n    c->ring[tail] = value;"),
    ("full-off-by-one", "if (next == atomic_load_explicit(&c->head, memory_order_acquire))",
     "if (tail == atomic_load_explicit(&c->head, memory_order_acquire))"),
    ("send-after-close", "if (atomic_load_explicit(&c->closed, memory_order_acquire))\n        return SC_CHAN_CLOSED;",
     "if (0)\n        return SC_CHAN_CLOSED;"),
]

MBOX = [
    ("publish-relaxed", "atomic_store_explicit(&slot->seq, 2 * pos + 1, memory_order_release)",
     "atomic_store_explicit(&slot->seq, 2 * pos + 1, memory_order_relaxed)"),
    ("recv-seq-relaxed", "if (atomic_load_explicit(&slot->seq, memory_order_acquire) != 2 * head + 1)",
     "if (atomic_load_explicit(&slot->seq, memory_order_relaxed) != 2 * head + 1)"),
    ("free-relaxed", "atomic_store_explicit(&slot->seq, 2 * (head + m->cap), memory_order_release)",
     "atomic_store_explicit(&slot->seq, 2 * (head + m->cap), memory_order_relaxed)"),
    ("claim-seq-relaxed", "atomic_load_explicit(&m->slots[p & (m->cap - 1)].seq, memory_order_acquire)",
     "atomic_load_explicit(&m->slots[p & (m->cap - 1)].seq, memory_order_relaxed)"),
    ("closed-with-claims", "if ((tail & 1) && (tail >> 1) == head)", "if (tail & 1)"),
    ("one-seq-per-position", "atomic_store_explicit(&slot->seq, 2 * (head + m->cap), memory_order_release)",
     "atomic_store_explicit(&slot->seq, 2 * (head + m->cap) - 1, memory_order_release)"),
    ("payload-after-publish",
     "    slot->value = value;\n    atomic_store_explicit(&slot->seq, 2 * pos + 1, memory_order_release);",
     "    atomic_store_explicit(&slot->seq, 2 * pos + 1, memory_order_release);\n    slot->value = value;"),
]


class GenmcMutantTests(unittest.TestCase):
    def test_every_mutant_is_rejected(self):
        argv = ["python3", "scripts/genmc.py", "--fail-fast"]
        jobs = [(f"{module}-{name}", (DIRS, f"stdlib/{source}", old, new, argv, ENV,
                                      LOGS / f"{module}-{name}.log"))
                for module, source, mutants in (("chan", "chan.c", CHAN), ("mbox", "mailbox.c", MBOX))
                for name, old, new in mutants]
        require_red(self, jobs, "FAIL")


if __name__ == "__main__":
    unittest.main()
