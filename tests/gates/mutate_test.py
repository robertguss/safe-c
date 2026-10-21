#!/usr/bin/env python3
"""Check that `make mutate` rejects weak contracts, using a fake and the real prover."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import mutate  # noqa: E402

SAMPLE = '''#include <stddef.h>
/*@ ensures a < b; */
int f(int a, int b) {
    // a < b in a comment
    if (a < 0 || b == 3)
        return 0;
    const char *s = "<=";
    return a + b;
}
'''


class MutantGenerationTests(unittest.TestCase):
    def kinds(self, text=SAMPLE):
        return [(kind.rsplit(" #", 1)[0], ln) for kind, ln, _ in mutate.mutants(text)]

    def test_mutates_code_only(self):
        kinds = self.kinds()
        self.assertIn(("cmp <-><=", 5), kinds)
        self.assertIn(("cmp ==->!=", 5), kinds)
        self.assertIn(("arith +", 8), kinds)
        self.assertIn(("if-cond->0", 5), kinds)
        self.assertEqual(sum(k == "drop-clause" for k, _ in kinds), 2)
        # ACSL, comments, strings and preprocessor lines are never touched.
        self.assertFalse([ln for _, ln in kinds if ln in (1, 2, 4, 7)])

    def test_every_mutant_differs_and_is_unique(self):
        texts = [m for _, _, m in mutate.mutants(SAMPLE)]
        self.assertEqual(len(texts), len(set(texts)))
        self.assertNotIn(SAMPLE, texts)

    def test_stdlib_has_mutants(self):
        for path in sorted((ROOT / "stdlib").glob("*.c")):
            if path.name != "chan.c":
                self.assertTrue(mutate.mutants(path.read_text()), path)

    def test_shards_split_the_run_exactly_once(self):
        sources = [f"stdlib/{p.name}" for p in sorted((ROOT / "stdlib").glob("*.c")) if p.name != "chan.c"]
        whole = {s: len(mutate.mutants((ROOT / s).read_text())) for s in sources}
        picked = [(s, i) for k in range(1, 5) for s, idx in mutate.shard_plan(sources, (k, 4)).items()
                  for i in idx]
        self.assertEqual(sorted(picked), sorted((s, i) for s, n in whole.items() for i in range(n)))
        sizes = [sum(map(len, mutate.shard_plan(sources, (k, 4)).values())) for k in range(1, 5)]
        self.assertLessEqual(max(sizes) - min(sizes), 1)
        self.assertEqual(mutate.shard_plan(sources, None), {s: None for s in sources})

    def test_shard_spec(self):
        self.assertEqual(mutate.parse_shard("2/6"), (2, 6))
        self.assertIsNone(mutate.parse_shard(""))
        for bad in ("0/3", "4/3", "x", "1-3"):
            with self.assertRaises(Exception):
                mutate.parse_shard(bad)

    def test_classify(self):
        self.assertEqual(mutate.classify(0, ""), "survived")
        self.assertEqual(mutate.classify(1, "unproved a"), "killed")
        self.assertEqual(mutate.classify(2, "error: unsupported proof warning on x"), "killed")
        self.assertEqual(mutate.classify(2, "error: Frama-C failed on x"), "invalid")


class MutateRunTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix="safe-c-mutate-test-")
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        for name in ("scripts", "include", "tests/gates"):
            shutil.copytree(ROOT / name, self.root / name)
        shutil.copy(ROOT / "Makefile", self.root / "Makefile")
        (self.root / "tests/mutate-equivalent.toml").write_text("")
        (self.root / "waivers.toml").write_text("")
        fake = self.root / "bin" / "frama-c"
        fake.parent.mkdir()
        fake.write_text('''#!/usr/bin/env python3
import json, os, pathlib, sys
src = pathlib.Path(sys.argv[-1])
ok = os.environ["MODE"] == "weak" or src.read_text() == pathlib.Path(os.environ["ORIGINAL"]).read_text()
pathlib.Path(sys.argv[sys.argv.index("-wp-report-json") + 1]).write_text(json.dumps([
    {"goal": "typed_f_ensures", "file": str(src), "function": "f", "passed": ok,
     "smoke": False, "verdict": "valid" if ok else "failed"}]))
''')
        fake.chmod(0o755)
        for name in ("alt-ergo", "z3"):
            tool = fake.parent / name
            tool.write_text("#!/bin/sh\nexit 0\n")
            tool.chmod(0o755)
        self.env = os.environ.copy()
        self.env.update(PATH=str(fake.parent) + os.pathsep + self.env["PATH"], FRAMA_C=str(fake))

    def make_mutate(self, mode, source, **env):
        original = self.root / source
        result = subprocess.run(
            ["make", "mutate", f"MUTATE_SRC={source}"], cwd=self.root, capture_output=True, text=True,
            env={**self.env, "MODE": mode, "ORIGINAL": str(original), "MUTATE_JOBS": "4", **env}, timeout=120)
        return result.returncode, result.stdout + result.stderr

    def test_weak_contract_fails_and_names_survivors(self):
        status, output = self.make_mutate("weak", "tests/gates/mutate-weak.c")
        self.assertNotEqual(status, 0, output)
        self.assertIn("survived tests/gates/mutate-weak.c:", output)

    def test_strong_contract_passes(self):
        status, output = self.make_mutate("strong", "tests/gates/mutate-strong.c")
        self.assertEqual(status, 0, output)
        self.assertIn("0 survived", output)

    def test_equivalent_list_accepts_only_exact_entries(self):
        source = "tests/gates/mutate-weak.c"
        planned = mutate.mutants((ROOT / source).read_text())
        entries = "".join(
            f'[[equivalent]]\nfile = "{source}"\nline = "{ln}"\nkind = "{kind}"\nreason = "test"\n'
            for kind, ln, _ in planned)
        (self.root / "tests/mutate-equivalent.toml").write_text(entries)
        status, output = self.make_mutate("weak", source)
        self.assertEqual(status, 0, output)
        (self.root / "tests/mutate-equivalent.toml").write_text(entries.replace('line = "', 'line = "9', 1))
        self.assertNotEqual(self.make_mutate("weak", source)[0], 0)

    def test_baseline_that_does_not_prove_is_an_error(self):
        status, output = self.make_mutate("strong", "tests/gates/mutate-strong.c")
        self.assertEqual(status, 0, output)
        (self.root / "tests/gates/mutate-strong.c").write_text("int x;\n")
        status, output = self.make_mutate("strong", "tests/gates/mutate-strong.c", ORIGINAL="/dev/null")
        self.assertNotEqual(status, 0, output)
        self.assertIn("does not prove", output)


@unittest.skipUnless(shutil.which("frama-c"), "frama-c not installed")
class RealProverTests(unittest.TestCase):
    """Run WP itself on the fixtures, waiving only the two tool-model RTE gaps."""

    def run_mutate(self, source):
        with tempfile.TemporaryDirectory(prefix="safe-c-mutate-real-") as tmp:
            waivers = Path(tmp) / "waivers.toml"
            waivers.write_text("".join(
                f'[[waiver]]\nid = "t-{goal}"\nfile = "{source}"\nfunction = "<translation-unit>"\n'
                f'goal = "{goal}"\nreason = "fixture"\nowner = "Safe C tests"\nexpires = "9999-12-31"\n'
                for goal in ("wp-rte-unaligned", "wp-rte-function-pointer")))
            result = subprocess.run([sys.executable, str(ROOT / "scripts/mutate.py"), "--waivers", str(waivers), source],
                                    cwd=ROOT, capture_output=True, text=True, timeout=900)
        return result.returncode, result.stdout + result.stderr

    def test_weak_fixture_fails(self):
        status, output = self.run_mutate("tests/gates/mutate-weak.c")
        self.assertEqual(status, 1, output)
        self.assertIn("survived tests/gates/mutate-weak.c:", output)

    def test_strong_fixture_passes(self):
        status, output = self.run_mutate("tests/gates/mutate-strong.c")
        self.assertEqual(status, 0, output)


if __name__ == "__main__":
    unittest.main()
