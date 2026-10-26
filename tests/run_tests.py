#!/usr/bin/env python3
"""Banc de test PC du VCU : compile et lance chaque secteur de test, puis les verifications statiques.

Usage (depuis la racine du depot) :
    python tests/run_tests.py                 # tout
    python tests/run_tests.py apps fs         # seulement ces secteurs
    python tests/run_tests.py -v              # affiche aussi les traces des taches
    python tests/run_tests.py --liste         # liste des secteurs

Le code de src/ n'est jamais modifie : il est copie dans tests/build/app_src/.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INCLUDE = ROOT / "include"
TESTS = ROOT / "tests"
HOST = TESTS / "host"
COMMON = TESTS / "common"
BUILD = TESTS / "build"

sys.path.insert(0, str(COMMON))
from vcu_build import exe_name, prepare_app_sources, run  # noqa: E402

# nom -> (fichier de test, defines propres au secteur, description)
SECTORS = {
    "apps": ("test_apps.c", [], "Pedale d'accelerateur (APPS) et frein"),
    "couple": ("test_couple.c", [], "Consigne de couple"),
    "vitesse": ("test_vitesse.c", [], "Vitesse vehicule"),
    "can_rx": ("test_can_rx.c", [], "Reception CAN (TaskCanRx)"),
    "can_tx": ("test_can_tx.c", [], "Emission CAN (TaskCanTx)"),
    "chaine": ("test_chaine.c", [], "Chaine TRVP -> Main -> CanTx"),
    "demarrage": ("test_integration.c", [], "Demarrage (MainApp)"),
    "watchdog": ("test_watchdog.c", ["USE_WDG", "refreshIWDG=test_refreshIWDG"], "Watchdog"),
    "ihm": ("test_ihm.c", [], "IHM et sorties TOR"),
    "refroidissement": ("test_cooling_digital.c", [], "Refroidissement (test historique)"),
    "fs": ("test_fs.c", [], "Reglement FS 2026"),
}

# Le test historique du refroidissement embarque sa propre copie de updateDataTimeStatus
LEGACY_COOLING_SOURCES = ["TaskCoolingSystem.c", "TaskDigitalSignals.c", "queueHandles.c"]

LINE_RE = re.compile(r"^\[(PASS|FAIL|BLOQUE)\s*\]")
LEGACY_RE = re.compile(r"^(PASS|FAIL): ")


def include_flags():
    # Les faux en-tetes FreeRTOS passent avant include/ (qui contient un FreeRTOSConfig.h de demo)
    return [f"-I{HOST / 'include'}", f"-I{COMMON}", f"-I{INCLUDE}"]


def build_sector(cc, name, app_dir, includes):
    test_file, defines, _ = SECTORS[name]
    out_dir = BUILD / name
    out_dir.mkdir(parents=True, exist_ok=True)
    exe = exe_name(out_dir / f"test_{name}")
    dflags = [f"-D{d}" for d in defines]

    if name == "refroidissement":
        units = [(HOST / "src" / "freertos_stubs.c", []), (HOST / "src" / test_file, [])]
        units += [(app_dir / s, []) for s in LEGACY_COOLING_SOURCES]
    else:
        units = [(HOST / "src" / "freertos_stubs.c", ["-DTEST_LINK_REAL_UTILS"]),
                 (HOST / "src" / "console_stub.c", []),
                 (HOST / "src" / "can_stub.c", []),
                 (HOST / "src" / test_file, ["-Wall", "-Wno-unused-function"])]
        units += [(src, ["-w"]) for src in sorted(app_dir.glob("*.c"))]

    objects = []
    for source, extra in units:
        obj = out_dir / (source.stem + ".o")
        res = run([cc, "-std=gnu11", "-g", "-O0", *includes, *dflags, *extra, "-c", str(source), "-o", str(obj)])
        if res.returncode != 0:
            return None, f"compilation de {source.name} :\n{res.stderr.strip()}"
        objects.append(str(obj))

    res = run([cc, *objects, "-o", str(exe)])
    if res.returncode != 0:
        return None, f"edition de liens :\n{res.stderr.strip()}"
    return exe, None


def parse_output(name, stdout):
    """Renvoie (pass, fail, bloque, lignes de resultat)."""
    lines = stdout.splitlines()
    if name == "refroidissement":
        results = [l for l in lines if LEGACY_RE.match(l)]
        p = sum(1 for l in results if l.startswith("PASS"))
        f = sum(1 for l in results if l.startswith("FAIL"))
        return p, f, 0, [f"[{'PASS' if l.startswith('PASS') else 'FAIL':<6}] {l.split(': ', 1)[1]}"
                         for l in results if l.startswith("FAIL")] or [f"[PASS  ] {p} verifications"]
    summary = next((l for l in lines if l.startswith("RESULTAT|")), None)
    results = [l for l in lines if LINE_RE.match(l) or l.startswith("         ")]
    if summary is None:
        return None
    _, _, p, f, b = summary.split("|")
    return int(p), int(f), int(b), results


def main():
    parser = argparse.ArgumentParser(description="Banc de test PC du VCU")
    parser.add_argument("secteurs", nargs="*", help="secteurs a lancer (defaut : tous + statique)")
    parser.add_argument("--cc", default=os.environ.get("CC", "gcc"))
    parser.add_argument("--arm-gcc", default=None)
    parser.add_argument("-v", "--verbose", action="store_true", help="affiche les traces des taches")
    parser.add_argument("--liste", action="store_true")
    args = parser.parse_args()

    if args.liste:
        for name, (f, _, desc) in SECTORS.items():
            print(f"  {name:<16} {f:<26} {desc}")
        print(f"  {'statique':<16} {'static/check_static.py':<26} Compilation stricte, code et projet carte")
        return 0

    selected = args.secteurs or list(SECTORS) + ["statique"]
    unknown = [s for s in selected if s not in SECTORS and s != "statique"]
    if unknown:
        print(f"Secteur(s) inconnu(s) : {', '.join(unknown)} (voir --liste)")
        return 2

    if shutil.which(args.cc) is None:
        print(f"Compilateur introuvable : {args.cc}")
        return 2

    BUILD.mkdir(parents=True, exist_ok=True)
    app_dir = BUILD / "app_src"
    # console.c est remplace par tests/host/src/console_stub.c
    prepare_app_sources(app_dir, exclude=("console.c",))
    includes = include_flags()

    env = dict(os.environ)
    if args.verbose:
        env["VCU_TEST_VERBOSE"] = "1"

    summary = []
    for name in [s for s in selected if s in SECTORS]:
        exe, error = build_sector(args.cc, name, app_dir, includes)
        if error:
            print(f"\n=== {SECTORS[name][2]} ===\n[FAIL  ] build  -            {error}")
            summary.append((SECTORS[name][2], 0, 1, 0))
            continue
        try:
            proc = subprocess.run([str(exe)], capture_output=True, text=True, errors="replace",
                                  stdin=subprocess.DEVNULL, timeout=30, env=env)
            stdout = proc.stdout
        except subprocess.TimeoutExpired:
            stdout = ""
        parsed = parse_output(name, stdout)
        print(f"\n=== {SECTORS[name][2]} ===")
        if parsed is None:
            print("[FAIL  ] crash  -            le test s'est arrete avant la fin (plantage ou boucle infinie)")
            if args.verbose:
                print(stdout)
            summary.append((SECTORS[name][2], 0, 1, 0))
            continue
        p, f, b, lines = parsed
        if args.verbose:
            print(stdout)
        else:
            print("\n".join(lines))
        summary.append((SECTORS[name][2], p, f, b))

    if "statique" in selected:
        cmd = [sys.executable, str(TESTS / "static" / "check_static.py"), "--cc", args.cc]
        if args.arm_gcc:
            cmd += ["--arm-gcc", args.arm_gcc]
        proc = run(cmd)
        print(proc.stdout.rstrip())
        if proc.returncode not in (0, 1):
            print(proc.stderr)
        res = next((l for l in proc.stdout.splitlines() if l.startswith("RESULTAT|")), None)
        if res:
            _, _, p, f, b = res.split("|")
            summary.append(("Statique", int(p), int(f), int(b)))

    print("\n" + "=" * 62)
    print(f"{'Secteur':<40}{'PASS':>7}{'FAIL':>7}{'BLOQUE':>8}")
    print("-" * 62)
    for sector, p, f, b in summary:
        print(f"{sector:<40}{p:>7}{f:>7}{b:>8}")
    print("-" * 62)
    tp, tf, tb = (sum(s[i] for s in summary) for i in (1, 2, 3))
    print(f"{'TOTAL':<40}{tp:>7}{tf:>7}{tb:>8}")
    print("=" * 62)
    print("FAIL = probleme present (voir AUDIT_VCU.md) ; BLOQUE = test non concluant pour l'instant")
    return 0 if tf == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
