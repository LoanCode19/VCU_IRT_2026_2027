#!/usr/bin/env python3
"""Emulateur PC du VCU : compile le code VCU avec le vrai noyau FreeRTOS et lance les tests FreeRTOS.

Usage (depuis la racine du depot) :
    python Emulators/FreeRTOS_PC/run_emulator.py               # tous les tests de tests/
    python Emulators/FreeRTOS_PC/run_emulator.py demarrage     # tests/test_rtos_demarrage.c
    python Emulators/FreeRTOS_PC/run_emulator.py -v            # affiche aussi les traces du VCU
    python Emulators/FreeRTOS_PC/run_emulator.py -D USE_WDG    # ajoute un define (ex. watchdog)

Noyau : les sources FreeRTOS V10.3.1 du projet carte (VCU_STM32F407/Middlewares/...), identiques a
la version officielle, avec le port Windows officiel de la meme version (port/MSVC-MingW).
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

EMU = Path(__file__).resolve().parent
ROOT = EMU.parents[1]
KERNEL = ROOT / "VCU_STM32F407" / "Middlewares" / "Third_Party" / "FreeRTOS" / "Source"
PORT = EMU / "port" / "MSVC-MingW"
TESTS = EMU / "tests"
BUILD = EMU / "build"
COMMON = ROOT / "tests" / "common"

sys.path.insert(0, str(COMMON))
from vcu_build import exe_name, prepare_app_sources, run  # noqa: E402

KERNEL_SOURCES = [KERNEL / name for name in
                  ("tasks.c", "queue.c", "list.c", "timers.c", "event_groups.c", "stream_buffer.c")]
KERNEL_SOURCES += [KERNEL / "portable" / "MemMang" / "heap_4.c", PORT / "port.c"]
EMU_SOURCES = [EMU / "main_pc.c", EMU / "emu_can.c", EMU / "emu_trace.c", EMU / "console_pc.c"]

RESULT_RE = re.compile(r"^\[(PASS|FAIL|BLOQUE)\s*\]|^ {9}\S")


def compile_unit(cc, source, obj, flags):
    res = run([cc, "-std=gnu11", "-g", "-O0", *flags, "-c", str(source), "-o", str(obj)])
    if res.returncode != 0:
        raise RuntimeError(f"compilation de {source.name} :\n{res.stderr.strip()}")
    return obj


def build(cc, test_file, defines):
    # EMU en premier : son FreeRTOSConfig.h passe avant celui de include/ (config de demo), et son
    # portmacro.h (enveloppe du port officiel) avant tout autre
    includes = [f"-I{EMU}", f"-I{KERNEL / 'include'}", f"-I{COMMON}", f"-I{ROOT / 'include'}"]
    dflags = [f"-D{d}" for d in defines]
    out = BUILD / test_file.stem
    out.mkdir(parents=True, exist_ok=True)

    # console.c est remplace par console_pc.c (mutex obligatoire avec le port Windows)
    app_sources = prepare_app_sources(BUILD / "app_src", exclude=("console.c",))

    objects = []
    for src in KERNEL_SOURCES:
        objects.append(compile_unit(cc, src, out / f"kernel_{src.stem}.o", [*includes, "-w"]))
    for src in app_sources:
        objects.append(compile_unit(cc, src, out / f"app_{src.stem}.o", [*includes, *dflags, "-w"]))
    for src in [*EMU_SOURCES, test_file]:
        objects.append(compile_unit(cc, src, out / f"emu_{src.stem}.o", [*includes, *dflags, "-Wall"]))

    exe = exe_name(out / test_file.stem)
    res = run([cc, *map(str, objects), "-o", str(exe), "-lwinmm"])
    if res.returncode != 0:
        raise RuntimeError(f"edition de liens :\n{res.stderr.strip()}")
    return exe


def main():
    parser = argparse.ArgumentParser(description="Emulateur FreeRTOS du VCU")
    parser.add_argument("tests", nargs="*", help="noms des tests (test_rtos_<nom>.c), defaut : tous")
    parser.add_argument("--cc", default=os.environ.get("CC", "gcc"))
    parser.add_argument("-D", dest="defines", action="append", default=[], help="define supplementaire")
    parser.add_argument("-v", "--verbose", action="store_true", help="affiche les traces console_print du VCU")
    parser.add_argument("--timeout", type=int, default=60, help="duree max d'un test en secondes")
    args = parser.parse_args()

    if os.name != "nt":
        print("Seul le port Windows (MinGW) est fourni pour l'instant : voir Emulators/FreeRTOS_PC/README.md")
        return 2

    available = {p.stem.replace("test_rtos_", ""): p for p in sorted(TESTS.glob("test_rtos_*.c"))}
    selected = args.tests or list(available)
    unknown = [t for t in selected if t not in available]
    if unknown:
        print(f"Test(s) inconnu(s) : {', '.join(unknown)}. Disponibles : {', '.join(available)}")
        return 2

    env = dict(os.environ)
    if args.verbose:
        env["VCU_EMU_VERBOSE"] = "1"

    totals = [0, 0, 0]
    failed_runs = 0
    for name in selected:
        try:
            exe = build(args.cc, available[name], args.defines)
        except RuntimeError as err:
            print(f"\n=== {name} ===\n[FAIL  ] build  -            {err}")
            failed_runs += 1
            continue
        try:
            proc = subprocess.run([str(exe)], capture_output=True, text=True, errors="replace",
                                  stdin=subprocess.DEVNULL, timeout=args.timeout, env=env)
            stdout, stderr, code = proc.stdout, proc.stderr, proc.returncode
        except subprocess.TimeoutExpired as exc:
            stdout = exc.stdout.decode(errors="replace") if isinstance(exc.stdout, bytes) else (exc.stdout or "")
            stderr, code = f"arret apres {args.timeout} s (test bloque ?)", None

        lines = stdout.splitlines()
        summary = next((l for l in lines if l.startswith("RESULTAT|")), None)
        print("\n".join(l for l in lines if args.verbose or l.startswith("===") or RESULT_RE.match(l)))
        if summary is None:
            print(f"[FAIL  ] crash  -            le test ne s'est pas termine normalement (code {code}) {stderr.strip()}")
            failed_runs += 1
            continue
        _, _, p, f, b = summary.split("|")
        totals = [totals[0] + int(p), totals[1] + int(f), totals[2] + int(b)]

    print(f"\nTOTAL emulateur : {totals[0]} PASS, {totals[1]} FAIL, {totals[2]} BLOQUE"
          + (f", {failed_runs} test(s) non termine(s)" if failed_runs else ""))
    return 0 if totals[1] == 0 and failed_runs == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
