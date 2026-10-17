#!/usr/bin/env python3
"""Require that TLC rejects each planted bug in models/channel.tla."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
LOGS = ROOT / "build/models"
TRANSFER = 'owner\' = [owner EXCEPT ![m] = (@ \\ {"sender"}) \\cup {"queue"}]'
NO_TRANSFER = 'owner\' = [owner EXCEPT ![m] = @ \\cup {"queue"}]'


class ChannelModelTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix="safe-c-model-")
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        for name in ("scripts", "models"):
            shutil.copytree(ROOT / name, self.root / name)
        shutil.copy(ROOT / "Makefile", self.root / "Makefile")
        jar = ROOT / "build/tools/tla2tools.jar"
        if jar.is_file():
            (self.root / "build/tools").mkdir(parents=True)
            shutil.copy(jar, self.root / "build/tools/tla2tools.jar")
        LOGS.mkdir(parents=True, exist_ok=True)

    def edit(self, name, old, new):
        path = self.root / "models" / name
        text = path.read_text()
        self.assertEqual(text.count(old), 1, old)
        path.write_text(text.replace(old, new))

    def model(self, label, ok=False):
        result = subprocess.run(["make", "model"], cwd=self.root, capture_output=True,
                                text=True, timeout=300)
        output = result.stdout + result.stderr
        (LOGS / f"{label}.log").write_text(output)
        self.assertEqual(result.returncode == 0, ok, output)
        if not ok:
            self.assertNotIn("not found", output, output)
        return output

    def test_model_passes_at_queue_sizes_one_and_three(self):
        output = self.model("pass", ok=True)
        self.assertIn("model: QueueSize=1: pass", output)
        self.assertIn("model: QueueSize=3: pass", output)

    def test_send_without_transfer_breaks_no_double_owner(self):
        self.edit("channel.tla", TRANSFER, NO_TRANSFER)
        output = self.model("no-transfer")
        self.assertIn("Invariant NoDoubleOwner is violated", output)

    def test_send_without_transfer_breaks_no_use_after_send(self):
        # With NoDoubleOwner off, the same bug must still surface as a use after send.
        self.edit("channel.tla", TRANSFER, NO_TRANSFER)
        self.edit("channel.cfg", "    NoDoubleOwner\n", "")
        output = self.model("no-transfer-use")
        self.assertIn("Invariant NoUseAfterSend is violated", output)

    def test_unbounded_send_breaks_type_ok(self):
        self.edit("channel.tla", "    /\\ Len(queue) < QueueSize\n", "")
        output = self.model("unbounded")
        self.assertIn("Invariant TypeOK is violated", output)

    def test_no_drain_after_close_deadlocks(self):
        self.edit("channel.tla", "Recv ==\n    /\\ queue # <<>>\n",
                  "Recv ==\n    /\\ ~closed\n    /\\ queue # <<>>\n")
        output = self.model("no-drain")
        self.assertIn("Deadlock reached", output)

    def test_free_while_queued_breaks_owned_while_live(self):
        # Releasing a message without taking it from its holder loses track of it.
        self.edit("channel.tla", "    /\\ owner' = [owner EXCEPT ![m] = @ \\ {p}]\n",
                  "    /\\ UNCHANGED owner\n")
        output = self.model("free-keeps-owner")
        self.assertIn("Invariant OwnedWhileLive is violated", output)


if __name__ == "__main__":
    unittest.main()
