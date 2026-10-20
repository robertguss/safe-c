#!/usr/bin/env python3
"""scripts/guide-check.py must reject an example that is wrong, untagged or quoted wrongly."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HAVE_SEMGREP = shutil.which("semgrep") is not None or (Path.home() / ".local/bin/semgrep").exists()

GOOD = "```c good\n#include <safec/slice.h>\n\nbool ok(void) {\n    return true;\n}\n```\n"


class GuideCheckTests(unittest.TestCase):
    def setUp(self):
        (ROOT / "build").mkdir(exist_ok=True)
        tmp = tempfile.TemporaryDirectory(prefix="guide-check-", dir=ROOT / "build")
        self.addCleanup(tmp.cleanup)
        self.dir = Path(tmp.name)

    def check(self, markdown, lint=False):
        doc = self.dir / "doc.md"
        doc.write_text(markdown)
        argv = ["python3", "scripts/guide-check.py", "--work", str(self.dir / "work"), str(doc)]
        if not lint:
            argv.insert(2, "--no-lint")
        return subprocess.run(argv, cwd=ROOT, capture_output=True, text=True, timeout=300)

    def test_accepts_a_correct_document(self):
        quote = "```c quote include/safec/result.h\nsc_option_size sc_none_size(void);\n```\n"
        result = self.check(GOOD + quote)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_accepts_avoid_adapter_and_elision(self):
        avoid = "```c avoid\n#include <safec/str.h>\n\nbool f(sc_arena *a, sc_str s) {\n    return sc_str_eq(a, s, SC_STR_LIT(a, \"x\"));\n}\n```\n"
        adapter = "```c adapter\n#include <safec/arena.h>\n\nstatic uint8_t raw[8];\nuint8_t *get(void) {\n    return raw;\n}\n```\n"
        quote = "```c quote include/safec/result.h\nsc_option_size sc_some_size(size_t v);\n/* ... */\nsc_option_size sc_none_size(void);\n```\n"
        result = self.check(avoid + adapter + quote)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_rejects_a_fence_it_cannot_see(self):
        result = self.check("- item\n\n  ```c\n  int x;\n  ```\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("code fence", result.stderr)

    def test_rejects_an_untagged_block(self):
        result = self.check("```c\nint x;\n```\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("must be tagged", result.stderr)

    def test_rejects_code_that_does_not_compile(self):
        result = self.check("```c good\nint f(void) { return missing; }\n```\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("does not compile", result.stderr)

    def test_rejects_a_quote_that_is_not_in_the_file(self):
        result = self.check("```c quote include/safec/result.h\nsc_option_size sc_none_size(int);\n```\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("not found in include/safec/result.h", result.stderr)

    def test_rejects_quote_lines_out_of_order(self):
        quote = "```c quote include/safec/result.h\nsc_option_size sc_none_size(void);\nsc_option_size sc_some_size(size_t v);\n```\n"
        result = self.check(quote)
        self.assertEqual(result.returncode, 1)

    def test_rejects_an_unknown_rule_id(self):
        result = self.check("```c bad R99-nothing\nint f(void) { return 0; }\n```\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("rule id", result.stderr)

    @unittest.skipUnless(HAVE_SEMGREP, "semgrep is not installed; CI runs this case")
    def test_lint_decides_good_and_bad(self):
        clean_bad = "```c bad R1-no-subscript\nint f(void) { return 0; }\n```\n"
        result = self.check(clean_bad, lint=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("lint did not report R1-no-subscript", result.stderr)
        dirty_good = "```c good\nint f(int *p) { return p[0]; }\n```\n"
        result = self.check(dirty_good, lint=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("lint failed", result.stderr)


if __name__ == "__main__":
    sys.exit(unittest.main(verbosity=1))
