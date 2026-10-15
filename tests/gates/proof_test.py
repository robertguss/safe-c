#!/usr/bin/env python3
"""Check the proof gate's fail-closed report and waiver boundaries."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ProofTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix="safe-c-proof-test-")
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        shutil.copytree(ROOT / "scripts", self.root / "scripts")
        shutil.copy(ROOT / "Makefile", self.root / "Makefile")
        (self.root / "stdlib").mkdir()
        (self.root / "stdlib/test.c").write_text("int test(void) { return 0; }\n")
        (self.root / "waivers.toml").write_text("")
        self.bin = self.root / "bin"
        self.bin.mkdir()
        fake = self.bin / "frama-c"
        fake.write_text('''#!/usr/bin/env python3
import os, pathlib, sys
if os.environ.get('NO_REPORT') != '1':
    pathlib.Path(sys.argv[sys.argv.index('-wp-report-json')+1]).write_text(os.environ['REPORT'])
print(os.environ.get('WARNINGS', ''))
sys.exit(int(os.environ.get('TOOL_EXIT', '0')))
''')
        fake.chmod(0o755)
        for name in ("alt-ergo", "z3"):
            tool = self.bin / name
            tool.write_text("#!/bin/sh\nexit 0\n")
            tool.chmod(0o755)
        self.env = os.environ.copy()
        self.env.update(PATH=str(self.bin) + os.pathsep + self.env["PATH"], FRAMA_C=str(fake))
        self.goal = {"goal": "typed_test_ensures", "file": "stdlib/test.c", "function": "test",
                     "passed": False, "smoke": False, "verdict": "unknown"}

    def run_prove(self, ok=False, report=None, **env):
        self.env.update(REPORT=json.dumps([self.goal]) if report is None else report, **env)
        result = subprocess.run(["make", "prove"], cwd=self.root, env=self.env,
                                capture_output=True, text=True, timeout=10)
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode == 0, ok, output)
        self.assertEqual((self.root / "build/prove.log").exists(), True, output)
        return output

    def waiver(self, file="stdlib/test.c", function="test", goal="typed_test_ensures", expires="9999-12-31"):
        return f'''[[waiver]]
id = "test-gap"
file = "{file}"
function = "{function}"
goal = "{goal}"
reason = "Test fixture"
owner = "Safe C tests"
expires = "{expires}"
'''

    def test_unproved_and_exact_waiver(self):
        self.assertIn("unproved stdlib/test.c :: test :: typed_test_ensures", self.run_prove())
        (self.root / "waivers.toml").write_text(self.waiver())
        self.assertIn("waived test-gap", self.run_prove(ok=True))
        for field in ("file", "function", "goal"):
            (self.root / "waivers.toml").write_text(self.waiver(**{field: "different"}))
            self.run_prove()

    def test_valid_goal_and_invalid_report(self):
        self.goal.update(passed=True, verdict="valid")
        self.run_prove(ok=True)
        for report in ("[]", "{}", "not json", '[{"passed":true}]'):
            self.run_prove(report=report)
        self.run_prove(NO_REPORT="1")

    def test_stale_report_cannot_mask_tool_failure(self):
        self.goal.update(passed=True, verdict="valid")
        self.run_prove(ok=True)
        self.run_prove(NO_REPORT="1", TOOL_EXIT="1")

    def test_missing_tool(self):
        self.assertIn("missing required tool: frama-c", self.run_prove(FRAMA_C="/no/such/frama-c"))

    def test_bad_waivers(self):
        for content in (self.waiver(expires="2000-01-01"), self.waiver(expires="bad"),
                        self.waiver(file="../test.c"), self.waiver().replace('owner = "Safe C tests"', ''),
                        self.waiver() * 2, '[waiver]\nid="typo"\n', '[[waivers]]\nid="typo"\n'):
            (self.root / "waivers.toml").write_text(content)
            self.run_prove()

    def test_skipped_guards_are_gaps(self):
        self.goal.update(passed=True, verdict="valid")
        warning = '[wp] Warning: Skipped RTE guards: unaligned pointers (\\aligned not supported)'
        output = self.run_prove(WARNINGS=warning)
        self.assertIn('unproved stdlib/test.c :: <translation-unit> :: wp-rte-unaligned', output)
        # Explicit alignment contracts hit the same unsupported model as RTE.
        warning += '\n[wp] stdlib/test.c:1: Warning: \\aligned not yet implemented'
        self.run_prove(WARNINGS=warning)
        (self.root / "waivers.toml").write_text(self.waiver(function="<translation-unit>", goal="wp-rte-unaligned"))
        self.assertIn("1 waived, 0 unproved", self.run_prove(ok=True))
        self.run_prove(WARNINGS='[wp] Warning: Missing RTE guards')


if __name__ == '__main__':
    unittest.main()
