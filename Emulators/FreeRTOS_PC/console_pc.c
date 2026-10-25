#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "console.h"

// Remplace src/console.c dans l'emulateur.
//
// Avec le port Windows, chaque tache est un thread que le noyau peut suspendre n'importe ou,
// y compris au milieu d'un printf. Toutes les sorties (traces du VCU et resultats des tests via
// VT_VPRINTF) passent donc par ce mutex FreeRTOS : une seule tache a la fois est dans printf.
//
// Les traces du VCU sont jetees par defaut (elles noieraient les resultats) et affichees si la
// variable d'environnement VCU_EMU_VERBOSE est definie.

static SemaphoreHandle_t sConsoleMutex = NULL;
static StaticSemaphore_t sConsoleMutexBuffer;
static bool sVerbose = false;

void console_init(void)
{
    sConsoleMutex = xSemaphoreCreateMutexStatic(&sConsoleMutexBuffer);
    sVerbose = (getenv("VCU_EMU_VERBOSE") != NULL);
}

// Utilisee par tests/common/vcu_test.h (VT_VPRINTF) pour les resultats des tests
int emu_vprintf(const char *fmt, va_list args)
{
    bool locked = (sConsoleMutex != NULL) && (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) &&
                  (xSemaphoreTake(sConsoleMutex, portMAX_DELAY) == pdTRUE);
    int written = vprintf(fmt, args);
    fflush(stdout);
    if (locked)
    {
        xSemaphoreGive(sConsoleMutex);
    }
    return written;
}

static int emu_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int written = emu_vprintf(fmt, args);
    va_end(args);
    return written;
}

void console_print(const char *fmt, ...)
{
    if (!sVerbose)
    {
        return;
    }

    char line[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    (void)emu_printf("    | %s", line);
}
