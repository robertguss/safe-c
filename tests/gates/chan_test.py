#!/usr/bin/env python3
"""Require that the channel's tests and ThreadSanitizer reject planted bugs in stdlib/chan.c."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
RELEASE = "atomic_store_explicit(&c->tail, next, memory_order_release)"
RELAXED = "atomic_store_explicit(&c->tail, next, memory_order_relaxed)"
FREE = "    if (!sc_handle_free(a, t, h))\n        return SC_CHAN_STALE;\n"
NO_FREE = "    (void)sc_handle_free;\n"
FULL = "    if (next == atomic_load_explicit(&c->head, memory_order_acquire))\n        return SC_CHAN_FULL;\n"
LIFO = "*value = c->ring[head];"
NEWEST = "*value = c->ring[(c->entries + atomic_load_explicit(&c->tail, memory_order_relaxed) - 1) % c->entries];"
CLOSED = "    if (atomic_load_explicit(&c->closed, memory_order_acquire))\n        return SC_CHAN_CLOSED;\n"
RECHECK = "        if (head == atomic_load_explicit(&c->tail, memory_order_acquire))\n            return SC_CHAN_CLOSED;\n"
NOSLOT = "        return SC_CHAN_NOSLOT;\n"
CLOSE = "atomic_store_explicit(&c->closed, true, memory_order_release);"


class ChannelMutantTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix="safe-c-chan-")
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        for name in ("scripts", "include", "stdlib", "tests"):
            shutil.copytree(ROOT / name, self.root / name)

    def mutate(self, old, new):
        path = self.root / "stdlib/chan.c"
        text = path.read_text()
        self.assertEqual(text.count(old), 1, old)
        path.write_text(text.replace(old, new))

    def run_gate(self, mode):
        return subprocess.run(["python3", "scripts/sanitize.py", mode], cwd=self.root,
                              capture_output=True, text=True, timeout=300)

    def test_prove_skips_only_the_channel(self):
        result = subprocess.run(
            ["python3", "-c",
             "import sys; sys.path.insert(0, 'scripts'); import prove; "
             "from pathlib import Path; "
             "kept = {p.as_posix() for p in prove.default_sources()}; "
             "every = {p.as_posix() for p in Path('stdlib').glob('*.c')}; "
             "print(sorted(every - kept))"],
            cwd=self.root, capture_output=True, text=True)
        self.assertEqual(result.stdout.strip(), "['stdlib/chan.c']", result.stderr)

    def test_unmutated_passes(self):
        self.assertEqual(self.run_gate("test").returncode, 0)
        self.assertEqual(self.run_gate("tsan").returncode, 0)

    def test_tsan_rejects_relaxed_publish(self):
        self.mutate(RELEASE, RELAXED)
        self.mutate("if (head == atomic_load_explicit(&c->tail, memory_order_acquire)) {\n",
                    "if (head == atomic_load_explicit(&c->tail, memory_order_relaxed)) {\n")
        result = self.run_gate("tsan")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ThreadSanitizer: data race", result.stdout + result.stderr)

    def test_tests_reject_send_that_keeps_the_handle(self):
        self.mutate(FREE, NO_FREE)
        self.assertNotEqual(self.run_gate("test").returncode, 0)

    def test_tests_reject_send_into_full_queue(self):
        self.mutate(FULL, "")
        self.assertNotEqual(self.run_gate("test").returncode, 0)

    def test_tests_reject_send_after_close(self):
        self.mutate(CLOSED, "")
        self.assertNotEqual(self.run_gate("test").returncode, 0)

    def test_tests_reject_recv_that_reports_open_after_close(self):
        self.mutate(RECHECK, "        return SC_CHAN_EMPTY;\n")
        self.assertNotEqual(self.run_gate("test").returncode, 0)

    def test_tests_reject_recv_that_drops_the_message_without_a_slot(self):
        self.mutate(NOSLOT, "        (void)0;\n")
        self.assertNotEqual(self.run_gate("test").returncode, 0)

    def test_tests_reject_close_that_does_not_close(self):
        self.mutate(CLOSE, "(void)c;")
        self.assertNotEqual(self.run_gate("test").returncode, 0)

    def test_tests_reject_lifo(self):
        self.mutate(LIFO, NEWEST)
        self.assertNotEqual(self.run_gate("test").returncode, 0)


if __name__ == "__main__":
    unittest.main()
