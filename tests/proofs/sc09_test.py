#!/usr/bin/env python3
"""Require WP to reject handle mutants, an unchecked unwrap, and a weakened get."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
LOGS = ROOT / "build/sc09"
GUARD = "    if (!sc_option_size_is_some(o))\n        return t.cap;\n"


class SC09ProofTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix="safe-c-sc09-")
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        for name in ("scripts", "include", "stdlib", "tests/proofs"):
            shutil.copytree(ROOT / name, self.root / name)
        for name in ("Makefile", "waivers.toml"):
            shutil.copy(ROOT / name, self.root / name)
        LOGS.mkdir(parents=True, exist_ok=True)

    def prove(self, label, source, ok=False):
        result = subprocess.run(["make", "prove", f"PROVE_SRC={source}"], cwd=self.root,
                                capture_output=True, text=True, timeout=300)
        output = result.stdout + result.stderr
        (LOGS / f"{label}.log").write_text(output)
        self.assertIn("prove: ", output, output)
        self.assertEqual(result.returncode == 0, ok, output)
        unproved = [line for line in output.splitlines() if line.startswith("unproved ")]
        return [line for line in unproved if " :: wp-rte-" not in line]

    def test_module_and_clients_prove(self):
        # Waivers cover only the two skipped RTE guards on stdlib/handle.c.
        self.assertEqual(self.prove("module", "stdlib/handle.c", ok=True), [])
        # The client file is not under stdlib/, so its two skipped guards stay unwaived.
        self.assertEqual(self.prove("clients", "tests/proofs/handle_client.c"), [])

    def test_every_function_has_a_rejected_mutant(self):
        mutants = (
            ("sc_handle_table_new", "        s.ptr[i] = 0;", "        s.ptr[i] = 1;"),
            ("sc_handle_alloc", "    out->generation = slot_generation(t, i);",
             "    out->generation = 0;"),
            ("sc_handle_alloc", "t.slots.ptr[3 * i] != SLOT_FREE)", "t.slots.ptr[3 * i] == SLOT_LIVE)"),
            ("sc_handle_free",
             "    if (!table_live(a, t.slots) || h.index >= t.cap)\n        return false;",
             "    if (h.index >= t.cap)\n        return false;"),
            ("sc_handle_free", "if (h.generation == SC_HANDLE_GEN_MAX) {",
             "if (h.generation == SC_HANDLE_GEN_MAX - 1) {"),
            ("sc_handle_free", "        t.slots.ptr[at + 1] = 0;", "        t.slots.ptr[at + 1] = 1;"),
            ("sc_handle_get",
             "    if (!table_live(a, t.slots) || h.index >= t.cap)\n        return sc_none_size();",
             "    if (h.index >= t.cap)\n        return sc_none_size();"),
            ("sc_handle_get",
             " || slot_generation(t, h.index) != h.generation)\n        return sc_none_size();",
             ")\n        return sc_none_size();"),
        )
        path = self.root / "stdlib/handle.c"
        original = path.read_text()
        for n, (function, old, new) in enumerate(mutants):
            with self.subTest(function=function, mutant=n):
                self.assertEqual(original.count(old), 1, old)
                path.write_text(original.replace(old, new, 1))
                failures = self.prove(f"mutant-{n}-{function}", "stdlib/handle.c")
                self.assertTrue(any(f" :: {function} :: " in f for f in failures), failures)
        path.write_text(original)

    def test_unchecked_unwrap_fails(self):
        path = self.root / "tests/proofs/handle_client.c"
        original = path.read_text()
        self.assertIn(GUARD, original)
        path.write_text(original.replace(GUARD, "", 1))
        failures = self.prove("unchecked-unwrap", "tests/proofs/handle_client.c")
        self.assertTrue(any(" :: checked_handle_client :: " in f and "requires" in f
                            for f in failures), failures)

    def test_get_contract_cannot_be_weakened(self):
        path = self.root / "include/safec/handle.h"
        original = path.read_text()
        old = r"ensures (\result.some != 0) <==> handle_live(a, t, h);"
        self.assertEqual(original.count(old), 1)
        path.write_text(original.replace(old, r"ensures \true;", 1))
        failures = self.prove("get-weakened", "tests/proofs/handle_client.c")
        self.assertTrue(any(" :: freed_handle_client :: " in f for f in failures), failures)


if __name__ == "__main__":
    unittest.main()
