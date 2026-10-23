#!/usr/bin/env python3
"""scripts/lint.py --verbose must print file:line rule for each finding, and plain output must not change."""

from pathlib import Path
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import lint  # noqa: E402

HAVE_TOOLS = all(shutil.which(tool) for tool in ("semgrep", "clang", "clang-tidy"))
FIXTURE = "tests/lint/bad/R1-no-subscript.c"


class ParserTests(unittest.TestCase):
    def test_clang_lines(self):
        out = "a.c:7:5: warning: variable length array used [-Wvla]\n"
        out += "a.c:9:3: warning: implicit conversion loses precision [-Wconversion]\n"
        self.assertEqual(
            lint.parse_clang_rules(out),
            {"R7-no-vla": {7}, "R11-no-implicit-conversion": {9}},
        )

    def test_tidy_lines(self):
        out = "a.c:4:1: warning: function 'f' is within a recursive call chain [misc-no-recursion]\n"
        out += "a.c:12:9: warning: narrowing conversion [bugprone-narrowing-conversions]\n"
        self.assertEqual(
            lint.parse_tidy_rules(out),
            {"R10-no-recursion": {4}, "tidy-narrowing": {12}},
        )


@unittest.skipUnless(HAVE_TOOLS, "semgrep, clang and clang-tidy are required")
class VerboseTests(unittest.TestCase):
    def run_lint(self, *flags):
        return subprocess.run(
            ["python3", "scripts/lint.py", *flags, FIXTURE],
            cwd=ROOT, capture_output=True, text=True, timeout=300,
        )

    def test_verbose_prints_file_line_rule(self):
        proc = self.run_lint("--verbose")
        self.assertEqual(proc.returncode, 1, proc.stderr)
        self.assertIn(f"{FIXTURE}:5 R1-no-subscript", proc.stdout.splitlines())
        self.assertIn("lint-rule R1-no-subscript", proc.stdout.splitlines())

    def test_plain_output_has_no_lines(self):
        proc = self.run_lint()
        self.assertEqual(proc.returncode, 1, proc.stderr)
        self.assertEqual(
            proc.stdout.splitlines(), [f"fail {FIXTURE}", "lint-rule R1-no-subscript"]
        )


if __name__ == "__main__":
    unittest.main()
