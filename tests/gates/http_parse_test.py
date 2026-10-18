#!/usr/bin/env python3
"""Require that tests/http_parse_test.c rejects planted bugs in examples/http/parse.c.

The parser is application code, so WP does not cover it; its unit test is its
evidence, and this suite shows the test goes red when the parser is wrong."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CC = shutil.which("clang") or shutil.which("clang-18") or "clang"

# (name, old, new): each old occurs exactly once in parse.c, or `count` times.
MUTANTS = [
    ("request line limit off by one", "i - pos > HTTP_MAX_LINE", "i - pos >= HTTP_MAX_LINE", 1),
    ("unterminated long line is only partial",
     "if (s.len - pos > HTTP_MAX_LINE)", "if (0)", 1),
    ("header count off by one", "r.header_count > HTTP_MAX_HEADERS", "r.header_count >= HTTP_MAX_HEADERS", 1),
    ("bytes after the blank line", "if (pos + 2 != head.len)", "if (0)", 1),
    ("head limit off by one", "head.len > HTTP_MAX_HEAD)", "head.len > HTTP_MAX_HEAD + 1)", 1),
    ("unfinished head at the limit", "return head.len >= HTTP_MAX_HEAD ? 431 : 400;", "return 400;", 2),
    ("values are not trimmed", "sc_slice_get(a, s, start, &b) && is_blank(b)", "sc_slice_get(a, s, start, &b) && 0", 1),
    ("target need not start with a slash", "first != '/'", "0", 1),
    ("HTTP/1.0 accepts anything", '|| SC_STR_EQ_LIT(a, r.version, "HTTP/1.0")', "|| 1", 1),
    ("HTTP/1.1 accepts anything", '!(SC_STR_EQ_LIT(a, r.version, "HTTP/1.1")', "!(1", 1),
    ("header names are not checked", "all_bytes(a, n, is_token)", "(n.len > 0)", 1),
    ("control bytes in values", "(is_control(b) && b != '\\t')", "0", 1),
    ("header lookup is case sensitive", "lower(p) != lower(q)", "p != q", 1),
    ("stale heads are readable", "!sc_slice_get(a, s, i, &b) || b == '\\n'", "b == '\\n'", 1),
    ("long request line is 400", "return line_error(st, 414);", "return line_error(st, 400);", 1),
]


class ParserMutantTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix="safe-c-http-")
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        for name in ("include", "stdlib", "examples/http"):
            shutil.copytree(ROOT / name, self.root / name)
        (self.root / "tests").mkdir()
        shutil.copy(ROOT / "tests/http_parse_test.c", self.root / "tests")

    def build_and_run(self):
        out = self.root / "http_parse_test"
        flags = ["-std=c11", "-g", "-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
        build = subprocess.run(
            [CC, *flags, "-Iinclude", "tests/http_parse_test.c", *map(str, sorted((self.root / "stdlib").glob("*.c"))),
             "-o", str(out)], cwd=self.root, capture_output=True, text=True)
        self.assertEqual(build.returncode, 0, build.stderr)
        return subprocess.run([str(out)], cwd=self.root, capture_output=True, text=True, timeout=120)

    def test_unmutated_passes(self):
        self.assertEqual(self.build_and_run().returncode, 0)

    def test_mutants_are_rejected(self):
        path = self.root / "examples/http/parse.c"
        original = path.read_text()
        for name, old, new, count in MUTANTS:
            with self.subTest(name):
                self.assertEqual(original.count(old), count, old)
                path.write_text(original.replace(old, new))
                self.assertNotEqual(self.build_and_run().returncode, 0, f"mutant survived: {name}")
        path.write_text(original)


if __name__ == "__main__":
    unittest.main()
