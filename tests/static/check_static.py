#!/usr/bin/env python3
"""Secteur "Statique" : verifications sans execution (compilation stricte, lecture du code et du projet carte).

Chaque verification porte l'identifiant du probleme dans AUDIT_VCU.md. Meme format de sortie que les
tests C (vcu_test.h), lu par tests/run_tests.py.

Usage : python tests/static/check_static.py [--cc gcc] [--arm-gcc chemin]
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "src"
INCLUDE = ROOT / "include"
HOST_INCLUDE = ROOT / "tests" / "host" / "include"
BOARD = ROOT / "VCU_STM32F407"

counts = {"PASS": 0, "FAIL": 0, "BLOQUE": 0}


def report(tag, test_id, rule, message):
    counts[tag] += 1
    print(f"[{tag:<6}] {test_id:<5} {rule:<12} {message}")


def check(test_id, rule, ok, message):
    report("PASS" if ok else "FAIL", test_id, rule, message)
    return ok


def read(path):
    return Path(path).read_text(encoding="utf-8", errors="replace")


def compile_diagnostics(cc, source, flags):
    """Compile un fichier en -fsyntax-only et renvoie (code retour, diagnostics)."""
    cmd = [cc, "-fsyntax-only", *flags, str(source)]
    proc = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    return proc.returncode, proc.stderr


def host_flags():
    return ["-std=gnu11", f"-I{HOST_INCLUDE}", f"-I{INCLUDE}"]


def app_sources():
    return sorted(SRC.glob("*.c"))


def find_arm_gcc(explicit):
    if explicit:
        return explicit
    if os.environ.get("ARM_GCC"):
        return os.environ["ARM_GCC"]
    found = shutil.which("arm-none-eabi-gcc")
    if found:
        return found
    pattern = "C:/ST/STM32CubeIDE*/STM32CubeIDE/plugins/*gnu-tools-for-stm32*/tools/bin/arm-none-eabi-gcc.exe"
    candidates = sorted(glob.glob(pattern))
    return candidates[-1] if candidates else None


def format_errors(cc, flags):
    """Erreurs de format printf, en donnant a console_print l'attribut format. Renvoie (total, fichiers)."""
    with tempfile.TemporaryDirectory() as tmp:
        fmt = Path(tmp) / "fmt_attr.h"
        fmt.write_text("void console_print(const char *fmt, ...) __attribute__((format(printf, 1, 2)));\n")
        total = 0
        files = []
        for src in app_sources():
            extra = ["-DUSE_WDG", "-DrefreshIWDG=test_refreshIWDG"] if src.name == "TaskWDG.c" else []
            _, diag = compile_diagnostics(cc, src, flags + extra + ["-include", str(fmt), "-Wformat=2"])
            n = sum(1 for l in diag.splitlines() if "-Wformat" in l)
            if n:
                total += n
                files.append(f"{src.name}({n})")
        return total, files


# ------------------------------------------------------------------------------------------------
# Compilation stricte (compilateur PC)

def check_compilation(cc):
    flags = host_flags()

    # Q1 : declaration juste apres un "case" (interdit avant C23)
    offenders = []
    for src in app_sources():
        _, diag = compile_diagnostics(cc, src, flags + ["-std=c11", "-pedantic"])
        hits = [l for l in diag.splitlines() if "label" in l and ("error" in l or "warning" in l)]
        if hits:
            offenders.append(f"{src.name}({len(hits)})")
    check("Q1a", "-", not offenders,
          "declarations apres un case (C11) : " + (", ".join(offenders) if offenders else "aucune"))

    # Q2 : formats printf (int32_t n'a pas le meme type sur PC et sur la carte : voir aussi Q2b)
    total, files = format_errors(cc, flags)
    check("Q2a", "-", total == 0,
          f"{total} erreur(s) de format printf (compilateur PC) : " + (", ".join(files) if files else "aucune"))

    # Q3 : variables masquees ou inutilisees
    total = 0
    for src in app_sources():
        _, diag = compile_diagnostics(cc, src, flags + ["-Wshadow", "-Wunused-variable", "-Wunused-but-set-variable"])
        total += sum(1 for l in diag.splitlines() if "warning" in l)
    check("Q3a", "-", total == 0, f"{total} variable(s) masquee(s) ou inutilisee(s) (-Wshadow -Wunused)")

    # W2 : TaskWDG.c doit compiler pour la carte (USE_WDG, sans LINUX)
    code, diag = compile_diagnostics(cc, SRC / "TaskWDG.c", flags + ["-DUSE_WDG"])
    first_error = next((l.strip() for l in diag.splitlines() if "error" in l), "")
    check("W2a", "-", code == 0,
          "TaskWDG.c avec USE_WDG (branche carte) : " + ("compile" if code == 0 else f"ne compile pas : {first_error}"))


def check_arm(arm_gcc):
    if not arm_gcc:
        report("BLOQUE", "Q2b", "-", "arm-none-eabi-gcc introuvable (variable ARM_GCC ou STM32CubeIDE)")
        report("BLOQUE", "C2b", "-", "arm-none-eabi-gcc introuvable (variable ARM_GCC ou STM32CubeIDE)")
        return
    rtos = BOARD / "Middlewares" / "Third_Party" / "FreeRTOS" / "Source"
    flags = [
        "-std=gnu11", "-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard",
        "-DSTM32F407xx", "-DUSE_HAL_DRIVER", "-Wall",
        f"-I{BOARD / 'Core' / 'Inc'}",
        f"-I{BOARD / 'Drivers' / 'STM32F4xx_HAL_Driver' / 'Inc'}",
        f"-I{BOARD / 'Drivers' / 'CMSIS' / 'Device' / 'ST' / 'STM32F4xx' / 'Include'}",
        f"-I{BOARD / 'Drivers' / 'CMSIS' / 'Include'}",
        f"-I{rtos / 'include'}", f"-I{rtos / 'CMSIS_RTOS'}", f"-I{rtos / 'portable' / 'GCC' / 'ARM_CM4F'}",
        f"-I{ROOT / 'Emulators'}", f"-I{INCLUDE}",
    ]

    # Q2 vu par la carte : int32_t y est un long, uint32_t un unsigned long
    total, files = format_errors(arm_gcc, flags)
    check("Q2b", "-", total == 0,
          f"{total} erreur(s) de format printf (compilateur carte) : " + (", ".join(files) if files else "aucune"))

    hidden = []
    for src in app_sources():
        _, diag = compile_diagnostics(arm_gcc, src, flags)
        for line in diag.splitlines():
            if "implicit-function-declaration" in line or "incompatible-pointer-types" in line:
                hidden.append(f"{src.name}: {line.split(': ', 2)[-1].strip()}")
    check("C2b", "-", not hidden,
          f"compilation carte sans les -Wno-* : {len(hidden)} diagnostic(s) masque(s)"
          + (" -> " + " | ".join(hidden[:3]) if hidden else ""))


# ------------------------------------------------------------------------------------------------
# Lecture du code VCU

def check_sources():
    vcu = read(SRC / "VCU.c")

    # B5 : structure de parametres locale (non static) passee a xTaskCreate
    locals_on_stack = re.findall(r"^\s+(?!static\b)(\w+Params?_t)\s+(\w+)\s*=", vcu, re.M)
    check("B5b", "-", not locals_on_stack,
          f"{len(locals_on_stack)} parametre(s) de tache en variable locale dans MainApp() : "
          + (", ".join(n for _, n in locals_on_stack) if locals_on_stack else "aucun"))

    # R5 : resultats de xTaskCreate / xQueueCreate non verifies
    ignored_tasks = len(re.findall(r"^\s*xTaskCreate\s*\(", vcu, re.M))
    queues = re.findall(r"^\s*(\w+)\s*=\s*xQueueCreate\s*\(", vcu, re.M)
    unchecked = [q for q in queues if not re.search(rf"({q}\s*==\s*NULL|configASSERT\s*\(\s*{q})", vcu)]
    check("R5a", "-", ignored_tasks == 0 and not unchecked,
          f"{ignored_tasks} xTaskCreate et {len(unchecked)} xQueueCreate sans verification du resultat")

    # R6 : configuration ambigue
    check("R6a", "-", not (INCLUDE / "FreeRTOSConfig.h").exists(),
          "include/FreeRTOSConfig.h (config de demo POSIX) coexiste avec celle de la carte")
    with_pthread = [p.name for p in app_sources() if re.search(r"#include\s*<pthread\.h>", read(p))]
    check("R6b", "-", not with_pthread, "pthread.h inclus dans : " + (", ".join(with_pthread) or "aucun"))
    check("R6c", "-", "vTaskStartScheduler" not in vcu,
          "MainApp() demarre lui-meme le scheduler (main.c appelle deja osKernelStart)")

    # R7 : mutex de console_print seulement sous LINUX
    console = read(SRC / "console.c")
    other_branch = console.split("#else", 1)[1] if "#else" in console else ""
    check("R7a", "-", "xSemaphoreTake" in other_branch or "#ifdef LINUX" not in console,
          "console_print sans mutex sur la carte (mutex seulement si LINUX)")

    # P3 : ancien emulateur inutilisable sur la carte
    isr_emu = read(ROOT / "Emulators" / "CanISREmulator.c")
    drv_emu = read(ROOT / "Emulators" / "CanRXDriverEmulator.c")
    isr_outside_linux = re.sub(r"#ifdef LINUX.*?#endif", "", isr_emu, flags=re.S)
    unusable = "CAN_ReadNext" in isr_outside_linux and "#ifdef LINUX" in drv_emu
    check("P3a", "-", not unusable,
          "TEST_ISR sans LINUX : CanISREmulator appelle CAN_ReadNext, defini seulement sous LINUX (ne linke pas)")


# ------------------------------------------------------------------------------------------------
# Projet carte VCU_STM32F407

def check_board():
    cproject = read(BOARD / ".cproject")
    configs = {}
    for block in cproject.split("<cconfiguration")[1:]:
        m = re.search(r'moduleId="org.eclipse.cdt.core.settings" name="(\w+)"', block)
        if m:
            configs[m.group(1)] = block

    debug = configs.get("Debug", "")
    flags = sorted(set(re.findall(r"-Wno-[\w-]+", debug)))
    check("C2a", "-", not flags, "options qui cachent des warnings (Debug) : " + (" ".join(flags) or "aucune"))

    release = configs.get("Release", "")
    check("C3a", "-", "../../include" in release and 'name="VCU_APP"' in release,
          "configuration Release : chemins et sources du code VCU " +
          ("presents" if "../../include" in release else "absents (VCU.h introuvable)"))

    main_c = read(BOARD / "Core" / "Src" / "main.c")
    isr = main_c.split("HAL_CAN_RxFifo0MsgPendingCallback", 1)[1].split("\n}", 1)[0] if \
        "HAL_CAN_RxFifo0MsgPendingCallback" in main_c else ""
    check("C1a", "-", "Error_Handler" not in isr,
          "ISR CAN : Error_Handler() appele dans l'interruption (le VCU se fige)")
    check("C1b", "-", "portYIELD_FROM_ISR" in isr,
          "ISR CAN : pas de portYIELD_FROM_ISR apres xQueueSendFromISR")

    # Le CAN ne doit demarrer qu'une fois les queues creees par MainApp() : sinon une trame arrivee
    # au demarrage est deposee dans une queue qui n'existe pas encore
    def body(code, signature):
        return code.split(signature, 1)[1].split("\n}", 1)[0] if signature in code else ""

    can_init = body(main_c, "static void MX_CAN1_Init(void)")
    main_body = body(main_c, "int main(void)")
    app_pos = main_body.find("MainApp();")
    start_pos = max(main_body.find("HAL_CAN_Start"), main_body.find("StartCan("))
    started_after = "HAL_CAN_Start" not in can_init and 0 <= app_pos < start_pos
    check("C1c", "-", started_after,
          "CAN demarre " + ("apres MainApp(), une fois les queues creees" if started_after
                            else "dans MX_CAN1_Init(), avant que MainApp() cree les queues"))
    check("C1d", "-", "CAN_IT_RX_FIFO1_MSG_PENDING" not in main_c,
          "notification FIFO1 activee : aucun filtre n'y envoie de trame et aucun callback ne la traite")
    check("C4a", "-", "CAN_MODE_LOOPBACK" not in main_c and "TaskSendTXMessages" not in main_c,
          "main.c : CAN en mode LOOPBACK et/ou tache de test TaskSendTXMessages encore presents")

    # C5 : le code genere de main.c doit correspondre au .ioc, sinon la prochaine generation CubeMX
    # ecrase sans prevenir les reglages faits a la main. Cle absente du .ioc : valeur par defaut de CubeMX.
    ioc = dict(line.split("=", 1) for line in read(BOARD / "VCU_STM32F407.ioc").splitlines() if "=" in line)
    can_fields = [("Prescaler", "Prescaler", "16"), ("TimeSeg1", "BS1", "CAN_BS1_1TQ"),
                  ("TimeSeg2", "BS2", "CAN_BS2_1TQ"), ("SyncJumpWidth", "SJW", "CAN_SJW_1TQ"),
                  ("Mode", "Mode", "CAN_MODE_NORMAL")]
    differences = []
    for field, key, default in can_fields:
        m = re.search(rf"hcan1\.Init\.{field}\s*=\s*(\w+)\s*;", main_c)
        code_value = m.group(1) if m else "absent"
        ioc_value = ioc.get(f"CAN1.{key}", default)
        if code_value != ioc_value:
            differences.append(f"{field} {code_value} (main.c) / {ioc_value} (.ioc)")
    check("C5a", "-", not differences,
          "CAN1 : main.c et .ioc " + ("identiques, " + ioc.get("CAN1.CalculateBaudRate", "?") + " bit/s"
                                     if not differences else "differents : " + ", ".join(differences)))

    # Taches et queues declarees dans le .ioc : creees dans main.c, pas commentees a la main
    tasks = [entry.split(",")[0] for entry in ioc.get("FREERTOS.Tasks01", "").split(";") if entry]
    queues = [entry.split(",")[0] for entry in ioc.get("FREERTOS.Queues01", "").split(";") if entry]
    missing = [n for n in tasks if not re.search(rf"^\s*osThreadDef\(\s*{n}\s*,", main_c, re.M)]
    missing += [n for n in queues if not re.search(rf"^\s*osMessageQDef\(\s*{n}\s*,", main_c, re.M)]
    check("C5b", "-", not missing,
          "taches et queues du .ioc creees dans main.c : "
          + ("oui (" + ", ".join(tasks + queues) + ")" if not missing else "commentees ou absentes : " + ", ".join(missing)))

    rtos_config = read(BOARD / "Core" / "Inc" / "FreeRTOSConfig.h")
    m = re.search(r"#define\s+configCHECK_FOR_STACK_OVERFLOW\s+(\d+)", rtos_config)
    check("R4a", "-", bool(m) and int(m.group(1)) > 0,
          "configCHECK_FOR_STACK_OVERFLOW " + (m.group(1) if m else "non defini") + " sur la carte (attendu 2)")

    # W2 (carte) : le vrai watchdog IWDG est actif, rafraichi par TaskWDG, et fige au debogueur
    missing = []
    if "IWDG" not in [v for k, v in ioc.items() if re.fullmatch(r"Mcu\.IP\d+", k)]:
        missing.append("IWDG active dans le .ioc")
    if not re.search(r"void\s+refreshIWDG\s*\(\s*void\s*\)\s*\{[^}]*HAL_IWDG_Refresh", main_c):
        missing.append("refreshIWDG() dans main.c")
    if "__HAL_DBGMCU_FREEZE_IWDG" not in main_c:
        missing.append("IWDG fige au debogueur")
    for name in ("Debug", "Release"):
        if 'value="USE_WDG"' not in configs.get(name, ""):
            missing.append(f"USE_WDG en {name}")
    check("W2b", "-", not missing,
          "IWDG de la carte actif et rafraichi : " + ("oui" if not missing else "manque " + ", ".join(missing)))


def main():
    print("\n=== Statique ===")
    check_compilation(ARGS.cc)
    check_arm(find_arm_gcc(ARGS.arm_gcc))
    check_sources()
    check_board()
    print(f"RESULTAT|Statique|{counts['PASS']}|{counts['FAIL']}|{counts['BLOQUE']}")
    return 0 if counts["FAIL"] == 0 else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--cc", default=os.environ.get("CC", "gcc"))
    parser.add_argument("--arm-gcc", default=None)
    ARGS = parser.parse_args()
    sys.exit(main())
