#!/usr/bin/env python3
"""Check epoch controls and require real WP failures for code/contract mutations."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
LOGS = ROOT / "build/sc05"


class SC05ProofTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix="safe-c-sc05-")
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        for name in ("scripts", "include", "stdlib", "tests/proofs"):
            shutil.copytree(ROOT / name, self.root / name)
        for name in ("Makefile", "waivers.toml"):
            shutil.copy(ROOT / name, self.root / name)
        LOGS.mkdir(parents=True, exist_ok=True)

    def prove(self, label, source):
        result = subprocess.run(["make", "prove", f"PROVE_SRC={source}"], cwd=self.root,
                                capture_output=True, text=True, timeout=180)
        output = result.stdout + result.stderr
        (LOGS / f"{label}.log").write_text(output)
        self.assertIn("prove: ", output, output)
        self.assertNotEqual(result.returncode, 0, "Model gaps must remain fail-closed")
        unproved = [line for line in output.splitlines() if line.startswith("unproved ")]
        gaps = [line for line in unproved if " :: wp-rte-" in line]
        self.assertEqual(len(gaps), 2, output)
        return [line for line in unproved if line not in gaps]

    def test_two_arena_epoch_controls(self):
        report = self.root / "epoch.json"
        result = subprocess.run([
            os.environ.get("FRAMA_C", "frama-c"), "-wp", "-wp-rte", "-warn-unsigned-overflow",
            "-wp-prover", "alt-ergo,z3", "-wp-timeout", "10", "-wp-report-json", str(report),
            "tests/proofs/ghost-two.c",
        ], cwd=self.root, capture_output=True, text=True, timeout=90)
        (LOGS / "ghost-two.log").write_text(result.stdout + result.stderr)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        goals = json.loads(report.read_text())
        self.assertTrue(goals)
        unproved = {g["goal"] for g in goals if not (g["passed"] and g["verdict"] == "valid")}
        self.assertEqual(unproved, {"typed_stale_own_call_slice_get_requires_live"})
        self.assertTrue(any(g["function"] == "fresh_other" for g in goals))
        self.assertTrue(any(g["function"] == "stale_recaptured" for g in goals))

    def test_code_mutations(self):
        for name, old, new, function in (
            ("arena", "a->used += rounded;", "a->used += n;", "sc_arena_alloc"),
            ("slice", "i < src.len; i++", "i <= src.len; i++", "sc_slice_copy"),
        ):
            with self.subTest(module=name):
                source = f"stdlib/{name}.c"
                baseline = self.prove(f"{name}-baseline", source)
                self.assertEqual(len(baseline), 2 if name == "arena" else 0, baseline)
                self.assertTrue(all("_base_alignment " in f or "_aligned_result " in f for f in baseline), baseline)
                path = self.root / source
                original = path.read_text()
                self.assertIn(old, original)
                path.write_text(original.replace(old, new, 1))
                failures = self.prove(f"{name}-mutation", source)
                additional = [f for f in failures if f not in baseline]
                self.assertTrue(any(f" :: {function} :: " in f for f in additional), failures)
                path.write_text(original)

    def test_contract_value_cannot_be_weakened(self):
        baseline = self.prove("client-baseline", "tests/proofs/contracts.c")
        self.assertEqual(len(baseline), 3, baseline)
        self.assertTrue(all("_requires_base_alignment" in f for f in baseline), baseline)
        path = self.root / "include/safec/slice.h"
        original = path.read_text()
        old = r"ensures \result ==> *out == s.ptr[i];"
        self.assertIn(old, original)
        path.write_text(original.replace(old, r"ensures \true;", 1))
        failures = self.prove("contract-weakened", "tests/proofs/contracts.c")
        self.assertTrue(any(" :: read_client :: " in f for f in failures), failures)

    def test_incomplete_allocation_behaviors_fail(self):
        path = self.root / "include/safec/arena.h"
        original = path.read_text()
        start = original.index("      assumes !(n > 0")
        end = original.index(";", start)
        path.write_text(original[:start] + "      assumes n == 0" + original[end:])
        failures = self.prove("behavior-incomplete", "stdlib/arena.c")
        self.assertTrue(any("_complete_" in f or "_complete " in f for f in failures), failures)

    def test_missing_assigns_fails(self):
        path = self.root / "include/safec/slice.h"
        original = path.read_text()
        self.assertIn("    assigns *out;", original)
        path.write_text(original.replace("    assigns *out;\n", "", 1))
        result = subprocess.run(["make", "prove", "PROVE_SRC=tests/proofs/contracts.c"],
                                cwd=self.root, capture_output=True, text=True, timeout=90)
        output = result.stdout + result.stderr
        (LOGS / "assigns-missing.log").write_text(output)
        self.assertNotEqual(result.returncode, 0)
        # A skipped RTE guard alone is insufficient evidence for this mutation.
        self.assertIn("Invalid infinite range", output, output)
        self.assertIn("Frama-C failed", output, output)


if __name__ == "__main__":
    unittest.main()
