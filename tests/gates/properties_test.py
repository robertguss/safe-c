#!/usr/bin/env python3
"""Check scripts/properties.py: hashes, coverage and assertion checks, and WP goal handling.

FakeFramaCTests run anywhere. WeakeningTests need Frama-C 33.0: each replaces one
`ensures` per module with `ensures \\true;` and requires that module's property
clients to go red.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import manifest as mf

CONTRACT = '''/*@ requires valid_arena(a) && slice_storage(a, {args});
    terminates \\true;
    exits \\false;
    assigns {assigns};
    ensures {ensures};
*/
'''
SLICE_CLIENTS = {
    "prop_get_keeps_out": ("s) && \\valid(out", "*out", "!\\result ==> *out == \\old(*out)",
                           "bool prop_get_keeps_out(const sc_arena *a, sc_slice s, size_t i, uint8_t *out) {\n"
                           "    return sc_slice_get(a, s, i, out);\n}\n"),
    "prop_sub_epoch": ("s", "\\nothing", "\\result.epoch == a->epoch",
                       "sc_slice prop_sub_epoch(const sc_arena *a, sc_slice s, size_t n) {\n"
                       "    return sc_slice_sub(a, s, 0, n);\n}\n"),
    "prop_copy_short": ("d) && slice_storage(a, s", "d.ptr[0 .. d.len - 1]", "s.len > d.len ==> !\\result",
                        "bool prop_copy_short(const sc_arena *a, sc_slice d, sc_slice s) {\n"
                        "    return sc_slice_copy(a, d, s);\n}\n"),
    "prop_move_short": ("d) && slice_storage(a, s", "d.ptr[0 .. d.len - 1]", "s.len > d.len ==> !\\result",
                        "bool prop_move_short(const sc_arena *a, sc_slice d, sc_slice s) {\n"
                        "    return sc_slice_move(a, d, s);\n}\n"),
}
FUNCTIONS = dict(zip(SLICE_CLIENTS, ("sc_slice_get", "sc_slice_sub", "sc_slice_copy", "sc_slice_move")))
FAKE = '''#!/usr/bin/env python3
import os, pathlib, sys
pathlib.Path(os.environ["CALLED"]).write_text("yes")
pathlib.Path(sys.argv[sys.argv.index("-wp-report-json") + 1]).write_text(os.environ["REPORT"])
print(os.environ.get("WARNINGS", ""))
'''
# One clause per module whose weakening some property must notice (KTD7).
WEAKENED = {
    "arena": ("arena.h", "ensures \\result <==> \\old(a->epoch) < UINT_MAX;"),
    "slice": ("slice.h", "ensures !\\result ==> *out == \\old(*out);"),
    "tslice": ("tslice.h", "ensures \\result.elem == elem && \\result.bytes.len == count * elem;"),
    "str": ("str.h", "ensures \\forall integer k; 0 <= k < n ==> \\result.ptr[k] == lit[k];"),
    "result": ("result.h", "ensures \\result.some != 0 && \\result.value == v;"),
    "handle": ("handle.h", "ensures (\\result.some != 0) <==> handle_live(a, t, h);"),
    "buf": ("buf.h", "ensures \\result ==> dec_at(b->dst.ptr + \\old(b->len), v);"),
    "utf8": ("utf8.h", "ensures valid_slice(a, s) ==> \\result == utf8_count(s.ptr, s.len);"),
}


def slice_props(**body):
    clients = {**SLICE_CLIENTS, **body}
    return '#include "safec/slice.h"\n\n' + "\n".join(
        CONTRACT.format(args=a, assigns=s, ensures=e) + code for a, s, e, code in clients.values())


def goal(function, name, passed=True, verdict="valid", smoke=False):
    return {"goal": f"typed_{function}_{name}", "file": "props.c", "function": function,
            "passed": passed, "smoke": smoke, "verdict": verdict}


def valid_report():
    return [g for name in SLICE_CLIENTS for g in (goal(name, "ensures"), goal(name, f"call_{FUNCTIONS[name]}_requires"),
                                                    goal(name, "wp_smoke_default_requires", smoke=True,
                                                         verdict="unknown"))]


class FakeFramaCTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix="safe-c-properties-test-")
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name) / "repo"
        for name in ("scripts", "include", "stdlib"):
            shutil.copytree(ROOT / name, self.root / name)
        (self.root / "waivers.toml").write_text("")
        self.write_module("slice", slice_props(), list(FUNCTIONS.items()))
        bin_dir = Path(tmp.name) / "bin"
        bin_dir.mkdir()
        for name, text in (("frama-c", FAKE), ("alt-ergo", "#!/bin/sh\n"), ("z3", "#!/bin/sh\n")):
            (bin_dir / name).write_text(text)
            (bin_dir / name).chmod(0o755)
        self.called = Path(tmp.name) / "called"
        self.env = {**os.environ, "PATH": f"{bin_dir}{os.pathsep}{os.environ['PATH']}",
                    "FRAMA_C": str(bin_dir / "frama-c"), "CALLED": str(self.called)}

    def write_module(self, module, props, clients):
        folder = self.root / "tests/properties" / module
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "brief.md").write_text(f"{module} brief\n")
        (folder / "props.c").write_text(props)
        entries = [{"id": c, "function": f, "statement": "s", "client": c, "author": "test"} for c, f in clients]
        data = {"module": module, **mf.hashes(module, self.root), "property": entries}
        (folder / "manifest.toml").write_text(mf.dump(data))

    def run_properties(self, code, *modules, report=None, **env):
        self.env.update(REPORT=json.dumps(valid_report() if report is None else report), **env)
        result = subprocess.run([sys.executable, "scripts/properties.py", *(modules or ("slice",))], cwd=self.root,
                                env=self.env, capture_output=True, text=True, timeout=30)
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode, code, output)
        return output

    def test_all_valid_passes(self):
        self.assertIn("properties: 8 goals proved, 0 failed", self.run_properties(0))

    def test_missing_rte_warning_is_accepted_and_others_fail(self):
        self.run_properties(0, WARNINGS="[wp] Warning: Missing RTE guards")
        self.assertIn("unsupported proof warning", self.run_properties(2, WARNINGS="[wp] Warning: Neither code nor"))

    def test_unproved_goal_is_unmet(self):
        report = valid_report()
        report[0].update(passed=False, verdict="unknown")
        self.assertIn("unmet slice :: prop_get_keeps_out :: typed_prop_get_keeps_out_ensures (unknown)",
                      self.run_properties(1, report=report))

    def test_proved_smoke_goal_is_vacuous(self):
        report = valid_report()
        report[2].update(passed=False, verdict="valid")
        self.assertIn("vacuous slice :: prop_get_keeps_out", self.run_properties(1, report=report))

    def test_zero_goals_is_a_tool_error(self):
        self.assertIn("WP reported no goals", self.run_properties(2, report=[]))

    def test_unasserted_before_proving(self):
        lazy = SLICE_CLIENTS["prop_sub_epoch"][3].replace("sc_slice_sub(a, s, 0, n)", "s")
        self.write_module("slice", slice_props(prop_sub_epoch=("s", "\\nothing", "\\true", lazy)),
                          list(FUNCTIONS.items()))
        output = self.run_properties(1)
        self.assertIn("unasserted slice :: prop_sub_epoch (prop_sub_epoch: never calls sc_slice_sub)", output)
        self.write_module("slice", slice_props(prop_sub_epoch=SLICE_CLIENTS["prop_sub_epoch"][:2] + (
            "\\true", SLICE_CLIENTS["prop_sub_epoch"][3])), list(FUNCTIONS.items()))
        self.assertIn("prop_sub_epoch: states nothing", self.run_properties(1))
        self.assertFalse(self.called.exists())

    def test_requires_goals_alone_are_unasserted(self):
        report = [g for g in valid_report() if g["goal"] != "typed_prop_copy_short_ensures"]
        self.assertIn("unasserted slice :: prop_copy_short (no ensures or assert goal of its own)",
                      self.run_properties(1, report=report))

    def test_hand_edit_is_stale_before_proving(self):
        props = self.root / "tests/properties/slice/props.c"
        props.write_text(props.read_text().replace("i, out", "i , out"))
        self.assertIn("stale slice :: props_sha256", self.run_properties(1))
        self.assertFalse(self.called.exists())

    def test_predicate_edit_stales_every_includer(self):
        self.write_module("str", '#include "safec/str.h"\n', [])
        header = self.root / "include/safec/slice.h"
        header.write_text(header.read_text().replace("a->used % 8 == 0 &&", "a->used % 4 == 0 &&", 1))
        output = self.run_properties(1, "slice", "str")
        for module in ("slice", "str"):
            self.assertIn(f"stale {module} :: include/safec/slice.h", output)

    def test_ensures_edit_keeps_hashes(self):
        header = self.root / "include/safec/slice.h"
        header.write_text(header.read_text().replace("ensures \\result.epoch == a->epoch;", "ensures \\true;", 1))
        self.run_properties(0)
        self.assertTrue(self.called.exists())

    def test_missing_entry_is_uncovered(self):
        self.write_module("slice", slice_props(), [i for i in FUNCTIONS.items() if i[1] != "sc_slice_sub"])
        self.assertIn("uncovered slice :: sc_slice_sub", self.run_properties(1))

    def test_missing_module_and_missing_tree(self):
        self.assertIn("uncovered arena :: no manifest", self.run_properties(1, "arena", "slice"))
        shutil.rmtree(self.root / "tests/properties")
        self.assertIn("tests/properties is missing", self.run_properties(2))

    def test_waiver_changes_nothing(self):
        (self.root / "waivers.toml").write_text('''[[waiver]]
id = "client-gap"
file = "tests/properties/slice/props.c"
function = "prop_get_keeps_out"
goal = "typed_prop_get_keeps_out_ensures"
reason = "Must not apply"
owner = "test"
expires = "9999-12-31"
''')
        report = valid_report()
        report[0].update(passed=False, verdict="unknown")
        self.assertIn("unmet slice :: prop_get_keeps_out", self.run_properties(1, report=report))


class WeakeningTests(unittest.TestCase):
    def test_each_module_notices_a_weakened_ensures(self):
        for module, (header, clause) in WEAKENED.items():
            with self.subTest(module=module), tempfile.TemporaryDirectory(prefix="safe-c-weaken-") as tmp:
                root = Path(tmp)
                for name in ("scripts", "include", "stdlib", "tests/properties"):
                    shutil.copytree(ROOT / name, root / name)
                path = root / "include/safec" / header
                text = path.read_text()
                self.assertEqual(text.count(clause), 1, clause)
                path.write_text(text.replace(clause, "ensures \\true;", 1))
                result = subprocess.run([sys.executable, "scripts/properties.py", module], cwd=root,
                                        capture_output=True, text=True, timeout=600)
                output = result.stdout + result.stderr
                self.assertEqual(result.returncode, 1, output)
                self.assertIn(f"unmet {module} :: ", output)


if __name__ == "__main__":
    unittest.main()
