#!/usr/bin/env python3
"""Require that TLC rejects each planted bug in models/mailbox.tla."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
LOGS = ROOT / "build/model/mutants"
TRANSFER = 'owner\' = [owner EXCEPT ![m] = (@ \\ {s}) \\cup {"queue"}]'
NO_TRANSFER = 'owner\' = [owner EXCEPT ![m] = @ \\cup {"queue"}]'
GUARD_PUBLISHED = "    /\\ Head(queue) \\in published\n"
SEND_OPEN = "Send(s, m) ==\n    /\\ ~closed\n"
HOLDS = "    /\\ s \\in owner[m]\n    /\\ Len(queue) < QueueSize\n"
DRAINED = "    /\\ queue = <<>>\n    /\\ reported' = TRUE\n"
PUBLISH_FREE = "Publish(m) ==\n"


class MailboxModelTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix="safe-c-mailbox-model-")
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

    def only(self, invariant):
        """Check one invariant, so TLC cannot report a different violation first."""
        path = self.root / "models/mailbox.cfg"
        text = path.read_text()
        head, rest = text.split("INVARIANTS\n")
        tail = rest.split("\nPROPERTIES", 1)[1]
        path.write_text(f"{head}INVARIANTS\n    {invariant}\n\nPROPERTIES{tail}")

    def model(self, label, ok=False):
        result = subprocess.run(["make", "model"], cwd=self.root, capture_output=True,
                                text=True, timeout=600,
                                env={**os.environ,
                                     "MODEL_QUEUE_SIZES": "2", "MODEL_SENDERS": "2"})
        output = result.stdout + result.stderr
        (LOGS / f"mailbox-{label}.log").write_text(output)
        self.assertEqual(result.returncode == 0, ok, output)
        if not ok:
            self.assertNotIn("not found", output, output)
        return output

    def test_model_passes_at_two_and_three_senders(self):
        result = subprocess.run(["make", "model"], cwd=ROOT, capture_output=True, text=True,
                                timeout=600)
        output = result.stdout + result.stderr
        (LOGS / "mailbox-pass.log").write_text(output)
        self.assertEqual(result.returncode, 0, output)
        for senders in (2, 3):
            for size in (1, 3):
                self.assertIn(f"model: mailbox: Senders={senders}, QueueSize={size}: pass", output)

    def test_send_without_transfer_breaks_no_double_owner(self):
        self.edit("mailbox.tla", TRANSFER, NO_TRANSFER)
        self.only("NoDoubleOwner")
        self.assertIn("Invariant NoDoubleOwner is violated", self.model("no-transfer"))

    def test_send_without_transfer_breaks_no_use_after_send(self):
        self.edit("mailbox.tla", TRANSFER, NO_TRANSFER)
        self.only("NoUseAfterSend")
        self.assertIn("Invariant NoUseAfterSend is violated", self.model("no-transfer-use"))

    def test_sending_anothers_message_breaks_no_double_owner(self):
        # With two senders, one can send a message the other holds.
        self.edit("mailbox.tla", HOLDS, "    /\\ Len(queue) < QueueSize\n")
        self.only("NoDoubleOwner")
        self.assertIn("Invariant NoDoubleOwner is violated", self.model("not-held"))

    def test_unbounded_send_breaks_type_ok(self):
        self.edit("mailbox.tla", "    /\\ Len(queue) < QueueSize\n", "")
        self.only("TypeOK")
        self.assertIn("Invariant TypeOK is violated", self.model("unbounded"))

    def test_send_after_close_breaks_nothing_after_close(self):
        self.edit("mailbox.tla", SEND_OPEN, "Send(s, m) ==\n")
        self.only("NothingAfterClose")
        self.assertIn("Invariant NothingAfterClose is violated", self.model("send-after-close"))

    def test_recv_before_publish_breaks_no_early_recv(self):
        # The ordering bug: the receiver reads a claimed place before the payload is written.
        self.edit("mailbox.tla", GUARD_PUBLISHED, "")
        self.only("NoEarlyRecv")
        self.assertIn("Invariant NoEarlyRecv is violated", self.model("early-recv"))

    def test_closed_report_with_claims_pending_breaks_no_lost_message(self):
        # Reporting closed before accepted sends are received loses them.
        self.edit("mailbox.tla", DRAINED, "    /\\ reported' = TRUE\n")
        self.only("NoLostMessage")
        self.assertIn("Invariant NoLostMessage is violated", self.model("report-early"))

    def test_recv_newest_breaks_fifo_order(self):
        self.edit("mailbox.tla", "Head(queue) \\in published", "queue[Len(queue)] \\in published")
        self.edit("mailbox.tla", "LET m == Head(queue) IN", "LET m == queue[Len(queue)] IN")
        self.edit("mailbox.tla", "queue' = Tail(queue)", "queue' = SubSeq(queue, 1, Len(queue) - 1)")
        self.only("FifoOrder")
        self.assertIn("Invariant FifoOrder is violated", self.model("lifo"))

    def test_no_publish_after_close_breaks_drains(self):
        # A sender that claimed a place before Close must still be able to publish.
        self.edit("mailbox.tla", PUBLISH_FREE, PUBLISH_FREE + "    /\\ ~closed\n")
        output = self.model("no-publish-after-close")
        self.assertTrue("Temporal properties were violated" in output or "Deadlock reached" in output,
                        output)

    def test_sender_cannot_free_after_close_breaks_drains(self):
        self.edit("mailbox.tla", "    /\\ p \\in owner[m]\n",
                  "    /\\ p \\in owner[m]\n    /\\ (p = \"receiver\" \\/ ~closed)\n")
        self.assertIn("Temporal properties were violated", self.model("stuck-shutdown"))


if __name__ == "__main__":
    unittest.main()
