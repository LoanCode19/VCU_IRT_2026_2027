"""Outils de compilation partages par tests/run_tests.py et Emulators/FreeRTOS_PC/run_emulator.py."""

import os
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "src"


def run(cmd, **kwargs):
    return subprocess.run(cmd, capture_output=True, text=True, errors="replace", **kwargs)


def prepare_app_sources(dest, exclude=()):
    """Copie src/*.c dans dest (src/ n'est jamais modifie) et renvoie la liste des fichiers copies."""
    dest = Path(dest)
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)

    copied = []
    for src in sorted(SRC.glob("*.c")):
        if src.name in exclude:
            continue
        shutil.copyfile(src, dest / src.name)
        copied.append(dest / src.name)
    return copied


def exe_name(path):
    path = Path(path)
    return path.with_suffix(".exe") if os.name == "nt" else path
