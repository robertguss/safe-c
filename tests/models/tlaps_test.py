#!/usr/bin/env python3
"""Require that the TLAPS proofs (scripts/tlaps.py) fail on planted bugs in the models."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests"))
from planted import require_red, tool_env

LOGS = ROOT / "build/tlaps/mutants"
ENV = tool_env("TLAPM", ROOT / "build/tools/tlapm/bin/tlapm")

# (label, model file, proof module, old text, new text)
MUTANTS = [
    # Reporting closed while accepted sends are still queued loses them (NoLostMessage).
    ("mailbox-report-early", "mailbox.tla", "mailbox_proofs",
     "    /\\ queue = <<>>\n    /\\ reported' = TRUE\n", "    /\\ reported' = TRUE\n"),
    # Sending after close breaks NothingAfterClose.
    ("channel-send-after-close", "channel.tla", "channel_proofs",
     "Send(m) ==\n    /\\ ~closed\n", "Send(m) ==\n"),
]


class TlapsMutantTests(unittest.TestCase):
    def test_every_mutant_breaks_its_proof(self):
        jobs = [(label, (("scripts", "models"), f"models/{model}", old, new,
                         ["python3", "scripts/tlaps.py", proof], ENV, LOGS / f"{label}.log"))
                for label, model, proof, old, new in MUTANTS]
        require_red(self, jobs, "obligations failed")


if __name__ == "__main__":
    unittest.main()
