#!/usr/bin/env python3
"""Require real WP failures when a tslice contract is mutated."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
LOGS = ROOT / "build/sc23"


class SC23ProofTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix="safe-c-sc23-")
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

    def test_baseline(self):
        self.assertEqual(self.prove("baseline", "stdlib/tslice.c", ok=True), [])

    def test_incomplete_allocation_behaviors_fail(self):
        path = self.root / "include/safec/tslice.h"
        original = path.read_text()
        start = original.index("      assumes !(elem > 0")
        end = original.index(";", start)
        path.write_text(original[:start] + "      assumes elem == 0" + original[end:])
        failures = self.prove("behavior-incomplete", "stdlib/tslice.c")
        self.assertTrue(any("_complete_" in f or "_complete " in f for f in failures), failures)

    def test_missing_assigns_fails(self):
        path = self.root / "include/safec/tslice.h"
        original = path.read_text()
        self.assertIn("    assigns out[0 .. elem - 1];\n", original)
        path.write_text(original.replace("    assigns out[0 .. elem - 1];\n", "", 1))
        client = self.root / "tests/proofs/tslice_client.c"
        client.parent.mkdir(parents=True, exist_ok=True)
        client.write_text(CLIENT)
        result = subprocess.run(["make", "prove", "PROVE_SRC=tests/proofs/tslice_client.c"],
                                cwd=self.root, capture_output=True, text=True, timeout=90)
        output = result.stdout + result.stderr
        (LOGS / "assigns-missing.log").write_text(output)
        self.assertNotEqual(result.returncode, 0)
        # A skipped RTE guard alone is insufficient evidence for this mutation.
        self.assertIn("Invalid infinite range", output, output)
        self.assertIn("Frama-C failed", output, output)

    def test_contract_weakened(self):
        # `ensures \true` for the get result: the client of the contract must go red.
        path = self.root / "include/safec/tslice.h"
        original = path.read_text()
        start = original.index("    ensures \\result <==> valid_slice(a, s.bytes)")
        end = original.index("*/", start)
        path.write_text(original[:start] + "    ensures \\true;\n" + original[end:])
        client = self.root / "tests/proofs/tslice_client.c"
        client.parent.mkdir(parents=True, exist_ok=True)
        client.write_text(CLIENT)
        failures = self.prove("contract-weakened", "tests/proofs/tslice_client.c")
        self.assertTrue(any(" :: get_client :: " in f for f in failures), failures)

    def test_contract_client_baseline(self):
        client = self.root / "tests/proofs/tslice_client.c"
        client.parent.mkdir(parents=True, exist_ok=True)
        client.write_text(CLIENT)
        # The client is not a stdlib file, so its two RTE guards stay unwaived and
        # make exits nonzero; only the contract goals must all be proved.
        self.assertEqual(self.prove("client-baseline", "tests/proofs/tslice_client.c"), [])


CLIENT = r'''#include "safec/tslice.h"

/*@ requires valid_arena(a) && slice_storage(a, s.bytes);
    requires elem > 0 ==> \valid(out + (0 .. elem - 1));
    requires elem > 0 ==> \separated(out + (0 .. elem - 1),
                                     a, a->base + (0 .. a->cap - 1));
    assigns out[0 .. elem - 1];
    ensures \result ==> i < s.bytes.len / elem;
*/
bool get_client(const sc_arena *a, sc_tslice s, size_t i, size_t elem, uint8_t *out) {
    return sc_tslice_get(a, s, i, elem, out);
}
'''

if __name__ == "__main__":
    unittest.main()
