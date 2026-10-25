#ifndef VCU_TEST_H
#define VCU_TEST_H

// Mini-framework des tests du VCU, partage par le banc PC (tests/host) et l'emulateur FreeRTOS
// (Emulators/FreeRTOS_PC).
//
// Chaque test porte l'identifiant du probleme dans AUDIT_VCU.md (S1a, B4a...) ou de l'article du
// reglement (FS-n), et la regle FS concernee. Un test exprime le comportement ATTENDU : il echoue
// tant que le probleme existe et passe une fois corrige.
//
// Trois verdicts :
//   PASS   : comportement attendu observe
//   FAIL   : probleme present
//   BLOQUE : le test ne peut pas conclure (ex. : la chaine ne produit jamais de couple,
//            donc "le couple est coupe" serait vrai pour une mauvaise raison)
//
// Sorties en ASCII uniquement (console Windows). L'emulateur redirige l'affichage via VT_VPRINTF
// (vers une fonction protegee par un mutex FreeRTOS).

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

#ifndef VT_VPRINTF
#define VT_VPRINTF vprintf
#endif

static int vtPassCount = 0;
static int vtFailCount = 0;
static int vtBlockedCount = 0;
static const char *vtSector = "";

static void vtPrintf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    VT_VPRINTF(fmt, args);
    va_end(args);
}

static void vtPrintResult(const char *tag, const char *id, const char *rule, const char *fmt, va_list args)
{
    char message[400];
    vsnprintf(message, sizeof(message), fmt, args);
    vtPrintf("[%-6s] %-5s %-12s %s\n", tag, id, rule, message);
}

static inline void vt_begin(const char *sector)
{
    vtSector = sector;
    vtPrintf("\n=== %s ===\n", sector);
}

// Verifie une condition. Renvoie la condition pour permettre de chainer des preconditions.
static inline bool vt_check(const char *id, const char *rule, bool ok, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vtPrintResult(ok ? "PASS" : "FAIL", id, rule, fmt, args);
    va_end(args);
    if (ok)
    {
        vtPassCount++;
    }
    else
    {
        vtFailCount++;
    }
    return ok;
}

static inline void vt_blocked(const char *id, const char *rule, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vtPrintResult("BLOQUE", id, rule, fmt, args);
    va_end(args);
    vtBlockedCount++;
}

// Detail affiche sous le dernier resultat (lignes indentees, conservees par les lanceurs)
static inline void vt_note(const char *fmt, ...)
{
    char message[400];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    vtPrintf("         %s\n", message);
}

// Derniere ligne lue par les lanceurs. Code de sortie 0 seulement si aucun FAIL.
static inline int vt_end(void)
{
    vtPrintf("RESULTAT|%s|%d|%d|%d\n", vtSector, vtPassCount, vtFailCount, vtBlockedCount);
    return (vtFailCount == 0) ? 0 : 1;
}

#endif // VCU_TEST_H
