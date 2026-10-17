#!/usr/bin/env python3
"""Require real WP failures when a string contract or its code is mutated."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
LOGS = ROOT / "build/sc06"


class SC06ProofTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix="safe-c-sc06-")
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        for name in ("scripts", "include", "stdlib"):
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

    def mutate(self, label, rel, old, new, function):
        path = self.root / rel
        original = path.read_text()
        self.assertIn(old, original)
        path.write_text(original.replace(old, new, 1))
        failures = self.prove(label, "stdlib/str.c")
        path.write_text(original)
        self.assertTrue(any(f" :: {function} :: " in f for f in failures), failures)

    def test_baseline(self):
        self.assertEqual(self.prove("baseline", "stdlib/str.c", ok=True), [])

    def test_code_mutants(self):
        for label, old, new, function in (
            ("eq-off-by-one", "i < x.len; i++", "i <= x.len; i++", "sc_str_eq"),
            ("eq-skip-length", "|| x.len != y.len", "", "sc_str_eq"),
            ("find-skip-last", "i <= last; i++", "i < last; i++", "sc_str_find"),
            ("find-wrong-index", "*out = i;", "*out = i + 1;", "sc_str_find"),
            ("lit-short-copy", "i < s.len; i++)\n        s.ptr[i] = lit[i];",
             "i + 1 < s.len; i++)\n        s.ptr[i] = lit[i];", "sc_str_from_lit"),
        ):
            with self.subTest(mutant=label):
                self.mutate(label, "stdlib/str.c", old, new, function)

    def test_contract_weakened(self):
        # `ensures \true` for the eq result: the client of the contract must go red.
        path = self.root / "include/safec/str.h"
        original = path.read_text()
        start = original.index("    ensures \\result <==> valid_slice(a, x)")
        end = original.index("*/", start)
        path.write_text(original[:start] + "    ensures \\true;\n" + original[end:])
        client = self.root / "tests/proofs/str_client.c"
        client.parent.mkdir(parents=True, exist_ok=True)
        client.write_text(CLIENT)
        failures = self.prove("contract-weakened", "tests/proofs/str_client.c")
        self.assertTrue(any(" :: eq_client :: " in f for f in failures), failures)

    def test_contract_client_baseline(self):
        client = self.root / "tests/proofs/str_client.c"
        client.parent.mkdir(parents=True, exist_ok=True)
        client.write_text(CLIENT)
        # The client is not a stdlib file, so its two RTE guards stay unwaived and
        # make exits nonzero; only the contract goals must all be proved.
        self.assertEqual(self.prove("client-baseline", "tests/proofs/str_client.c"), [])


CLIENT = r'''#include "safec/str.h"

/*@ requires valid_arena(a) && slice_storage(a, x) && slice_storage(a, y);
    assigns \nothing;
    ensures \result ==> x.len == y.len;
*/
bool eq_client(const sc_arena *a, sc_str x, sc_str y) {
    return sc_str_eq(a, x, y);
}
'''

if __name__ == "__main__":
    unittest.main()
