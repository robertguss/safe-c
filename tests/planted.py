"""Shared by the suites that plant one edit in a copy of the tree and require a tool to go red
(tests/gates/genmc_test.py, tests/models/tlaps_test.py)."""
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def tool_env(var: str, local: Path) -> dict:
    """The copy has no build/, so point it at this tree's tool unless var is set."""
    env = dict(os.environ)
    if local.is_file():
        env.setdefault(var, str(local))
    return env


def plant(dirs, path, old, new, argv, env, log):
    """Copy dirs (or files), replace old (which must occur once) with new in path, run argv there."""
    with tempfile.TemporaryDirectory(prefix="safe-c-planted-") as tmp:
        root = Path(tmp)
        for name in dirs:
            if (ROOT / name).is_dir():
                shutil.copytree(ROOT / name, root / name)
            else:
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy(ROOT / name, root / name)
        target = root / path
        text = target.read_text()
        if text.count(old) != 1:
            return -1, f"edit does not match exactly once in {path}: {old}"
        target.write_text(text.replace(old, new))
        result = subprocess.run(argv, cwd=root, capture_output=True, text=True, timeout=1800,
                                env=env)
        output = result.stdout + result.stderr
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(output)
        return result.returncode, output


def require_red(case, jobs, marker, workers=4):
    """Run each (label, plant-args) job in parallel; each must fail and print marker."""
    with ThreadPoolExecutor(max_workers=workers) as pool:
        results = list(pool.map(lambda job: (job[0], *plant(*job[1])), jobs))
    for label, code, output in results:
        with case.subTest(mutant=label):
            case.assertNotEqual(code, -1, output)
            case.assertNotIn("not found", output, output)
            case.assertNotEqual(code, 0, f"{label} survived:\n{output}")
            case.assertIn(marker, output, output)
