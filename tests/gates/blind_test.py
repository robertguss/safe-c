#!/usr/bin/env python3
"""Check scripts/blind.py: header stripping, Codex-first authoring, the Claude fallback and disputes."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import manifest

PROPS = '''#include "safec/slice.h"

/*@ requires valid_arena(a) && slice_storage(a, s) && \\valid(out);
    requires \\separated(out, a, a->base + (0 .. a->cap - 1));
    terminates \\true;
    exits \\false;
    assigns *out;
    ensures !\\result ==> *out == \\old(*out);
*/
bool prop_get_keeps_out(const sc_arena *a, sc_slice s, size_t i, uint8_t *out) {
    return sc_slice_get(a, s, i, out);
}
'''
ENTRY = '''[[property]]
id = "{id}"
function = "sc_slice_get"
statement = "A failed get leaves *out unchanged."
client = "prop_get_keeps_out"
'''
FAKE_CODEX = '''#!/usr/bin/env python3
import json, os, pathlib, sys
if sys.argv[1] == "setup":
    print(json.dumps({"ready": os.environ.get("FAKE_READY") == "1"}))
    sys.exit(0)
jobs = pathlib.Path(os.environ["CLAUDE_PLUGIN_DATA"]) / "state/ws/jobs"
jobs.mkdir(parents=True)
(jobs / "task-1.log").write_text(os.environ.get("FAKE_LOG", "ran in " + os.getcwd()))
print(json.dumps({"status": 0, "rawOutput": os.environ["FAKE_REPLY"]}))
sys.exit(int(os.environ.get("FAKE_EXIT", "0")))
'''
FAKE_CLAUDE = '''#!/usr/bin/env python3
import json, os, pathlib, sys
files = sorted(str(p.relative_to(os.getcwd())) for p in pathlib.Path(os.getcwd()).rglob("*") if p.is_file())
pathlib.Path(os.environ["FAKE_RECORD"]).write_text(json.dumps({"argv": sys.argv[1:], "cwd": os.getcwd(),
                                                                "files": files, "stdin": sys.stdin.read()}))
print(os.environ["FAKE_REPLY"])
'''


def reply(*ids, props=PROPS, head=""):
    return f"Here it is.\n\n```toml\n{head}{''.join(ENTRY.format(id=i) for i in ids)}```\n\n```c\n{props}```\n"


class StripTests(unittest.TestCase):
    def test_slice_keeps_declarations_and_predicates(self):
        text = manifest.strip_header((ROOT / "include/safec/slice.h").read_text())
        self.assertEqual(manifest.declared_functions(text),
                         ["sc_slice_get", "sc_slice_sub", "sc_slice_copy", "sc_slice_move"])
        for kept in ("typedef struct sc_arena", "} sc_slice;", "predicate valid_slice"):
            self.assertIn(kept, text)
        for gone in ("ensures", "requires", "assigns", "behavior", "Stale and out-of-range"):
            self.assertNotIn(gone, text)

    def test_handle_keeps_logic_block(self):
        text = manifest.strip_header((ROOT / "include/safec/handle.h").read_text())
        self.assertIn("logic integer slot_state", text)
        self.assertIn("predicate handle_live", text)
        self.assertNotIn("ensures", text)


class BlindTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix="safe-c-blind-test-")
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name) / "repo"
        for name in ("scripts", "include", "stdlib"):
            shutil.copytree(ROOT / name, self.root / name)
        self.folder = self.root / "tests/properties/slice"
        self.folder.mkdir(parents=True)
        (self.folder / "brief.md").write_text("SC-05: slices refuse stale and out-of-range reads.\n")
        bin_dir = Path(tmp.name) / "bin"
        bin_dir.mkdir()
        for name, text in (("codex", FAKE_CODEX), ("claude", FAKE_CLAUDE)):
            (bin_dir / name).write_text(text)
            (bin_dir / name).chmod(0o755)
        self.record = Path(tmp.name) / "claude.json"
        (Path(tmp.name) / ".codex").mkdir()
        (Path(tmp.name) / ".codex/config.toml").write_text('model = "gpt-test"\n')
        self.env = {**os.environ, "HOME": tmp.name, "BLIND_CODEX": str(bin_dir / "codex"),
                    "BLIND_CLAUDE": str(bin_dir / "claude"), "FAKE_RECORD": str(self.record),
                    "FAKE_READY": "1", "FAKE_REPLY": reply("get-keeps-out")}

    def blind(self, *args, ok=True, **env):
        self.env.update(env)
        result = subprocess.run([sys.executable, "scripts/blind.py", *args, "--code-model", "opus"],
                                cwd=self.root, env=self.env, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode == 0, ok, result.stdout + result.stderr)
        return result

    def manifest(self):
        return tomllib.loads((self.folder / "manifest.toml").read_text())

    def test_codex_writes_manifest_with_matching_hashes(self):
        self.blind("author", "slice", "--source", "SC-05=abc123@2026-05-01T10:00:00Z")
        data = self.manifest()
        self.assertEqual(data["run"][0]["provider"], "openai-codex")
        self.assertEqual(data["run"][0]["job_id"], "task-1")
        self.assertEqual(data["property"][0]["author"], "openai-codex:gpt-test")
        self.assertEqual(data["source"], [{"card": "SC-05", "page": "abc123", "edited": "2026-05-01T10:00:00Z"}])
        self.assertEqual((self.folder / "props.c").read_text(), PROPS)
        self.assertEqual({k: data[k] for k in ("brief_sha256", "props_sha256", "headers")},
                         manifest.hashes("slice", self.root))
        self.assertFalse(self.record.exists())

    def test_missing_companion_falls_back_to_claude(self):
        self.blind("author", "slice", BLIND_CODEX="")
        call = json.loads(self.record.read_text())
        self.assertEqual(call["argv"], ["-p", "--model", "sonnet", "--tools", "", "--strict-mcp-config",
                                        "--no-session-persistence"])
        data = self.manifest()
        self.assertEqual(data["run"][0]["fallback_reason"], "codex companion not found")
        self.assertEqual({e["author"] for e in data["property"]}, {"anthropic-claude:sonnet"})

    def test_model_sees_only_the_prompt_files(self):
        self.blind("author", "slice", FAKE_READY="0")
        call = json.loads(self.record.read_text())
        self.assertFalse(Path(call["cwd"]).resolve().is_relative_to(self.root.resolve()))
        self.assertEqual(call["files"], ["brief.md", "include/safec/slice.h", "prompt.md"])
        self.assertIn("slices refuse stale", call["stdin"])
        self.assertNotIn("ensures \\result <==> valid_slice(a, s)", call["stdin"])
        self.assertNotIn(str(self.root), call["stdin"])
        self.assertEqual(self.manifest()["run"][0]["fallback_reason"], "codex companion not ready")

    def test_fallback_update_keeps_codex_authors(self):
        self.blind("author", "slice")
        self.blind("update", "slice", BLIND_CODEX="", FAKE_REPLY=reply("get-keeps-out", "get-again"))
        authors = {e["id"]: e["author"] for e in self.manifest()["property"]}
        self.assertEqual(authors, {"get-keeps-out": "openai-codex:gpt-test", "get-again": "anthropic-claude:sonnet"})

    def test_log_naming_the_repo_fails(self):
        self.blind("author", "slice", ok=False, FAKE_LOG=f"cat {self.root}/include/safec/slice.h")
        self.assertFalse((self.folder / "manifest.toml").exists())
        self.assertTrue((self.folder / "codex.log").exists())

    def test_same_model_fallback_is_refused(self):
        result = subprocess.run([sys.executable, "scripts/blind.py", "author", "slice", "--code-model", "Sonnet"],
                                cwd=self.root, env=self.env, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 1)
        self.assertIn("code agent's model", result.stderr)
        self.assertFalse((self.folder / "reply.txt").exists())

    def test_reply_without_c_block_fails_without_fallback(self):
        bad = "```toml\n" + ENTRY.format(id="x") + "```\n"
        self.blind("author", "slice", ok=False, FAKE_REPLY=bad)
        self.assertEqual((self.folder / "reply.txt").read_text(), bad)
        self.assertFalse(self.record.exists())
        self.assertFalse((self.folder / "manifest.toml").exists())

    def test_dispute_keep_leaves_props_and_appends_record(self):
        self.blind("author", "slice")
        objection = self.root / "objection.md"
        objection.write_text("The brief never says out is left unchanged.\n")
        keep = '```toml\ndecision = "keep"\nreason = "The brief says failed reads change nothing."\n```\n'
        for n in (1, 2):
            self.blind("dispute", "slice", "--property", "get-keeps-out", "--objection", str(objection),
                       FAKE_REPLY=keep)
            self.assertEqual((self.folder / "props.c").read_text(), PROPS)
            self.assertEqual(len(self.manifest()["dispute"]), n)
        record = self.manifest()["dispute"][0]
        self.assertEqual((record["property"], record["decision"]), ("get-keeps-out", "keep"))
        self.assertIn("The brief never says", record["objection"])


if __name__ == "__main__":
    unittest.main()
