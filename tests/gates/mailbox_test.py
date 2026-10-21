#!/usr/bin/env python3
"""Require that the mailbox's tests and ThreadSanitizer reject planted bugs in stdlib/mailbox.c."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PUBLISH = "atomic_store_explicit(&slot->seq, 2 * pos + 1, memory_order_release)"
PUBLISH_RELAXED = "atomic_store_explicit(&slot->seq, 2 * pos + 1, memory_order_relaxed)"
READY = "if (atomic_load_explicit(&slot->seq, memory_order_acquire) != 2 * head + 1) {"
READY_RELAXED = "if (atomic_load_explicit(&slot->seq, memory_order_relaxed) != 2 * head + 1) {"
FREE = "    (void)sc_handle_free(a, t, h);\n"
NO_FREE = "    (void)sc_handle_free;\n"
FULL = "            return SC_MBOX_FULL;\n"
OPEN = "        if (t & 1)\n            return SC_MBOX_CLOSED;\n"
IN_FLIGHT = "        if ((tail & 1) && (tail >> 1) == head)\n"
NOSLOT = "        return SC_MBOX_NOSLOT;\n"
CLOSE = "atomic_fetch_or_explicit(&m->tail, 1, memory_order_release);"
CAS = """atomic_compare_exchange_weak_explicit(&m->tail, &t, t + 2,
                                                               memory_order_relaxed,
                                                               memory_order_relaxed)"""
RACY_CLAIM = "(atomic_store_explicit(&m->tail, t + 2, memory_order_relaxed), true)"
SEQ_FREE = "atomic_store_explicit(&slot->seq, 2 * (head + m->cap), memory_order_release);"


class MailboxMutantTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix="safe-c-mbox-")
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        for name in ("scripts", "include", "stdlib", "tests", "examples"):
            shutil.copytree(ROOT / name, self.root / name)

    def mutate(self, old, new):
        path = self.root / "stdlib/mailbox.c"
        text = path.read_text()
        self.assertEqual(text.count(old), 1, old)
        path.write_text(text.replace(old, new))

    def run_gate(self, mode):
        return subprocess.run(["python3", "scripts/sanitize.py", mode], cwd=self.root,
                              capture_output=True, text=True, timeout=300)

    def test_unmutated_passes(self):
        self.assertEqual(self.run_gate("test").returncode, 0)
        self.assertEqual(self.run_gate("tsan").returncode, 0)

    def test_tsan_rejects_relaxed_publish(self):
        self.mutate(PUBLISH, PUBLISH_RELAXED)
        self.mutate(READY, READY_RELAXED)
        result = self.run_gate("tsan")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ThreadSanitizer: data race", result.stdout + result.stderr)

    def test_tests_reject_claim_without_compare_and_swap(self):
        # Two senders claim the same place: messages are lost or duplicated.
        self.mutate(CAS, RACY_CLAIM)
        self.assertNotEqual(self.run_gate("test").returncode, 0)

    def test_tests_reject_send_that_keeps_the_handle(self):
        self.mutate(FREE, NO_FREE)
        self.assertNotEqual(self.run_gate("test").returncode, 0)

    def test_tests_reject_send_into_full_mailbox(self):
        self.mutate(FULL, "            return SC_MBOX_CLOSED;\n")
        self.assertNotEqual(self.run_gate("test").returncode, 0)

    def test_tests_reject_send_after_close(self):
        self.mutate(OPEN, "")
        self.assertNotEqual(self.run_gate("test").returncode, 0)

    def test_tests_reject_closed_while_an_accepted_send_is_in_flight(self):
        self.mutate(IN_FLIGHT, "        if (tail & 1)\n")
        self.assertNotEqual(self.run_gate("test").returncode, 0)

    def test_tests_reject_recv_that_drops_the_message_without_a_slot(self):
        self.mutate(NOSLOT, "        (void)0;\n")
        self.assertNotEqual(self.run_gate("test").returncode, 0)

    def test_tests_reject_close_that_does_not_close(self):
        self.mutate(CLOSE, "(void)m;")
        self.assertNotEqual(self.run_gate("test").returncode, 0)

    def test_tests_reject_recv_that_never_frees_the_slot(self):
        self.mutate(SEQ_FREE, "(void)slot;")
        self.assertNotEqual(self.run_gate("test").returncode, 0)


if __name__ == "__main__":
    unittest.main()
