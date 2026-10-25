"""Outils de compilation partages par tests/run_tests.py et Emulators/FreeRTOS_PC/run_emulator.py."""

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "src"
COMPAT = ROOT / "tests" / "host" / "compat"

# Declaration juste apres un "case" (probleme Q1) : refusee par gcc < 11 en C11
_CASE_DECL = re.compile(r"^(\s*case\s+[^:\n]+:)([ \t]*\n\s*(?:const\s+)?(?:int|float|double|bool|char|long|"
                        r"unsigned|signed|u?int\d+_t|[A-Za-z]\w*_t)\b)", re.M)


def run(cmd, **kwargs):
    return subprocess.run(cmd, capture_output=True, text=True, errors="replace", **kwargs)


def compiler_accepts(cc, code):
    with tempfile.TemporaryDirectory() as tmp:
        probe = Path(tmp) / "probe.c"
        probe.write_text(code)
        return run([cc, "-std=gnu11", "-fsyntax-only", str(probe)]).returncode == 0


def compat_include_flags(cc):
    """src/VCU.c inclut pthread.h (probleme R6) : MinGW n'en a pas, on fournit un fichier vide."""
    if compiler_accepts(cc, "#include <pthread.h>\nint main(void){return 0;}\n"):
        return []
    return [f"-I{COMPAT}"]


def prepare_app_sources(cc, dest, exclude=()):
    """Copie src/*.c dans dest (src/ n'est jamais modifie).

    Avec un gcc ancien, ajoute un ';' apres les "case" suivis d'une declaration (Q1) dans la copie.
    Renvoie (liste des fichiers copies, liste des fichiers corriges).
    """
    dest = Path(dest)
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)

    needs_q1 = not compiler_accepts(cc, "int f(int x){switch(x){case 1: int y = x; return y;} return 0;}\n")
    copied, patched = [], []
    for src in sorted(SRC.glob("*.c")):
        if src.name in exclude:
            continue
        text = src.read_text(encoding="utf-8", errors="replace")
        if needs_q1:
            text, n = _CASE_DECL.subn(r"\1;\2", text)
            if n:
                patched.append(f"{src.name}({n})")
        (dest / src.name).write_text(text, encoding="utf-8")
        copied.append(dest / src.name)
    return copied, patched


def exe_name(path):
    path = Path(path)
    return path.with_suffix(".exe") if os.name == "nt" else path
