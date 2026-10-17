#!/usr/bin/env python3
"""Require that WP rejects an unchecked unwrap and a weakened accessor contract."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
LOGS = ROOT / "build/sc07"
GUARD = "    if (!sc_result_size_is_ok(r))\n        return 0;\n"


class SC07ProofTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix="safe-c-sc07-")
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        for name in ("scripts", "include", "stdlib", "tests/proofs"):
            shutil.copytree(ROOT / name, self.root / name)
        for name in ("Makefile", "waivers.toml"):
            shutil.copy(ROOT / name, self.root / name)
        LOGS.mkdir(parents=True, exist_ok=True)

    def prove(self, label, source, ok=False):
        result = subprocess.run(["make", "prove", f"PROVE_SRC={source}"], cwd=self.root,
                                capture_output=True, text=True, timeout=180)
        output = result.stdout + result.stderr
        (LOGS / f"{label}.log").write_text(output)
        self.assertIn("prove: ", output, output)
        self.assertEqual(result.returncode == 0, ok, output)
        unproved = [line for line in output.splitlines() if line.startswith("unproved ")]
        return [line for line in unproved if " :: wp-rte-" not in line]

    def test_module_and_checked_client_prove(self):
        # Waivers cover only the two skipped RTE guards on stdlib/result.c.
        self.assertEqual(self.prove("module", "stdlib/result.c", ok=True), [])
        client = "tests/proofs/result_client.c"
        # The client file is not under stdlib/, so its two skipped guards stay unwaived.
        self.assertEqual(self.prove("checked-client", client), [])

    def test_unchecked_unwrap_fails(self):
        path = self.root / "tests/proofs/result_client.c"
        original = path.read_text()
        self.assertIn(GUARD, original)
        path.write_text(original.replace(GUARD, "", 1))
        failures = self.prove("unchecked-unwrap", "tests/proofs/result_client.c")
        self.assertTrue(any(" :: checked_result_client :: " in f and "requires" in f
                            for f in failures), failures)

    def test_every_function_has_a_rejected_mutant(self):
        mutants = (
            ("sc_some_size", "sc_option_size o = {true, v};", "sc_option_size o = {true, 0};"),
            ("sc_none_size", "sc_option_size o = {false, 0};", "sc_option_size o = {false, 1};"),
            ("sc_option_size_is_some", "    return o.some;", "    return !o.some;"),
            ("sc_option_size_unwrap",
             "sc_option_size_unwrap(sc_option_size o) {\n    return o.value;",
             "sc_option_size_unwrap(sc_option_size o) {\n    return 0;"),
            ("sc_option_size_or",
             "    if (o.some)\n        return o.value;\n    return fallback;",
             "    return fallback;"),
            ("sc_ok_size", "sc_result_size r = {true, v, 0};", "sc_result_size r = {true, v, 1};"),
            ("sc_err_size", "sc_result_size r = {false, 0, e};", "sc_result_size r = {false, 1, e};"),
            ("sc_result_size_is_ok", "    return r.ok;", "    return !r.ok;"),
            ("sc_result_size_unwrap",
             "sc_result_size_unwrap(sc_result_size r) {\n    return r.value;",
             "sc_result_size_unwrap(sc_result_size r) {\n    return 0;"),
            ("sc_result_size_unwrap_err", "    return r.err;", "    return 0;"),
            ("sc_result_size_or",
             "    if (r.ok)\n        return r.value;\n    return fallback;",
             "    return r.value;"),
        )
        path = self.root / "stdlib/result.c"
        original = path.read_text()
        for function, old, new in mutants:
            with self.subTest(function=function):
                self.assertEqual(original.count(old), 1, old)
                path.write_text(original.replace(old, new, 1))
                failures = self.prove(f"mutant-{function}", "stdlib/result.c")
                self.assertTrue(any(f" :: {function} :: " in f for f in failures), failures)
        path.write_text(original)

    def test_contract_value_cannot_be_weakened(self):
        path = self.root / "include/safec/result.h"
        original = path.read_text()
        old = r"ensures \result == r.value;"
        self.assertIn(old, original)
        path.write_text(original.replace(old, r"ensures \true;", 1))
        failures = self.prove("contract-weakened", "tests/proofs/result_client.c")
        self.assertTrue(any(" :: checked_result_client :: " in f for f in failures), failures)


if __name__ == "__main__":
    unittest.main()
