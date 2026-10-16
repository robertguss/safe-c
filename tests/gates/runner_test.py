#!/usr/bin/env python3
"""Exercise the public Make targets in disposable repositories with real clang."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class RunnerTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix="safe-c-runner-")
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        (self.root / "scripts").mkdir()
        for name in ("sanitize.py", "lint.py", "tools.py", "prove.py", "prove.sh", "waivers.py"):
            shutil.copy(ROOT / "scripts" / name, self.root / "scripts" / name)
        shutil.copy(ROOT / "Makefile", self.root / "Makefile")
        shutil.copy(ROOT / "waivers.toml", self.root / "waivers.toml")
        shutil.copytree(ROOT / "tests/gates", self.root / "tests/gates")
        (self.root / "fuzz").mkdir()
        self.env = os.environ.copy()
        for key in ("CC", "LLVM", "CFLAGS", "FUZZ_SECONDS", "MAKEFLAGS", "MAKEOVERRIDES"):
            self.env.pop(key, None)

    def make(self, *args, ok=True):
        result = subprocess.run(["make", *args], cwd=self.root, env=self.env,
                                capture_output=True, text=True, timeout=60)
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode == 0, ok, output)
        return output

    def test_empty_catalogs_and_canaries(self):
        output = self.make("test", "fuzz", "gate-selftest")
        self.assertIn("test: no *_test.c sources", output)
        self.assertIn("fuzz: no fuzz/*_fuzz.c harnesses", output)
        for name in ("asan", "ubsan", "fuzz"):
            self.assertIn(f"{name} canary caught", output)

    def test_tests_flags_and_failure(self):
        src = self.root / "tests/space name_test.c"
        src.write_text('#ifndef OVERRIDE\n#error missing CFLAGS\n#endif\nint main(void) { return 0; }\n')
        self.make("test", "CFLAGS=-std=c11 -DOVERRIDE")
        src.write_text("int main(void) { return 7; }\n")
        self.make("test", ok=False)
        shutil.copy(self.root / "tests/gates/asan-canary.c", src)
        self.assertIn("AddressSanitizer", self.make("test", ok=False))

    def test_fixture_trees_are_not_tests(self):
        (self.root / "tests/lint").mkdir()
        (self.root / "tests/lint/sneaky_test.c").write_text("int main(void) { return 1; }\n")
        (self.root / "tests/gates/also_test.c").write_text("int main(void) { return 1; }\n")
        nested = self.root / "tests/mod/ok_test.c"
        nested.parent.mkdir()
        nested.write_text("int main(void) { return 0; }\n")
        output = self.make("test")
        self.assertIn("pass tests/mod/ok_test.c", output)
        self.assertNotIn("sneaky_test.c", output)
        self.assertNotIn("also_test.c", output)

    def test_production_fuzz_and_failure(self):
        src = self.root / "fuzz/space name_fuzz.c"
        src.write_text('''#ifndef OVERRIDE
#error missing CFLAGS
#endif
#include <stddef.h>
#include <stdint.h>
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    (void)data; (void)size; return 0;
}
''')
        self.make("fuzz", "FUZZ_SECONDS=1", "CFLAGS=-std=c11 -DOVERRIDE")
        # An unconditional sanitizer fault must fail the production target.
        canary = (self.root / "tests/gates/canary_fuzz.c").read_text()
        src.write_text(canary.replace('size > 0U && *data == 0x41U', 'size > 0U && *data <= 0xffU'))
        self.assertIn("AddressSanitizer", self.make("fuzz", "FUZZ_SECONDS=1", ok=False))

    def test_library_headers_and_sources_are_linked(self):
        (self.root / "include").mkdir()
        (self.root / "stdlib").mkdir()
        (self.root / "include/example.h").write_text("int example(void);\n")
        (self.root / "stdlib/example.c").write_text("int example(void) { return 7; }\n")
        (self.root / "tests/library_test.c").write_text(
            '#include "example.h"\nint main(void) { return example() == 7 ? 0 : 1; }\n')
        (self.root / "fuzz/library_fuzz.c").write_text(
            '#include "example.h"\n#include <stddef.h>\n#include <stdint.h>\n#include <assert.h>\n'
            'int LLVMFuzzerTestOneInput(const uint8_t *p, size_t n) {\n'
            '    (void)p; (void)n; assert(example() == 7); return 0; }\n')
        self.make("test", "fuzz", "FUZZ_SECONDS=1")

    def test_missing_compiler_and_invalid_budget(self):
        for mode in ("test", "fuzz", "gate-selftest"):
            self.assertIn("missing required tool", self.make(mode, "CC=/no/such/clang", ok=False))
        for value in ("0", "-1", "nope"):
            self.assertIn("positive integer", self.make("fuzz", f"FUZZ_SECONDS={value}", ok=False))

    def test_canary_requires_sanitizer_report(self):
        src = self.root / "tests/gates/canary_fuzz.c"
        src.write_text(src.read_text().replace('return ptr[index];', '(void)ptr; (void)index; __builtin_trap();'))
        self.assertIn("fuzz canary was not caught", self.make("gate-selftest", ok=False))

    def test_wp_canary_cannot_be_weakened(self):
        src = self.root / "tests/gates/wp-canary.c"
        src.write_text(src.read_text().replace(r'ensures canary: \result == 1;', r'ensures canary: \true;'))
        self.assertIn("WP canary was not caught", self.make("gate-selftest", ok=False))

    def test_fuzz_is_not_application_code(self):
        (self.root / "fuzz/input_fuzz.c").write_text("int a[1];\n")
        (self.root / "app.c").write_text("int main(void) { return 0; }\n")
        result = subprocess.run(
            [sys.executable, "-c", "import sys; sys.path.insert(0, 'scripts'); "
             "import lint; assert [p.name for p in lint.application_sources()] == ['app.c']"],
            cwd=self.root, capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_gates_run_all_stages_and_stop_on_failure(self):
        # Stub lint only: this checks Make ordering without requiring lint's toolchain.
        (self.root / "scripts/lint.py").write_text(
            "import sys\n"
            "print('waivers checked' if '--waivers' in sys.argv else 'lint checked', flush=True)\n"
        )
        output = self.make("-j4", "gates")
        markers = ("lint checked", "test: no", "asan canary caught", "ubsan canary caught",
                   "fuzz canary caught", "fuzz: no", "waivers: 0 active", "prove: no")
        positions = [output.index(m) for m in markers]
        self.assertEqual(positions, sorted(positions))
        (self.root / "fuzz/fail_fuzz.c").write_text("#error broken production harness\n")
        output = self.make("gates", ok=False)
        self.assertNotIn("prove: no", output)


if __name__ == "__main__":
    unittest.main()
