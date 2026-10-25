#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "console.h"

// Remplace src/console.c : les taches affichent beaucoup de traces a chaque passage.
// Elles sont jetees, sauf si la variable d'environnement VCU_TEST_VERBOSE est definie.

void console_init(void)
{
}

void console_print(const char *fmt, ...)
{
    static int verbose = -1;
    if (verbose < 0)
    {
        verbose = (getenv("VCU_TEST_VERBOSE") != NULL) ? 1 : 0;
    }
    if (verbose == 0)
    {
        return;
    }

    va_list args;
    va_start(args, fmt);
    printf("    | ");
    vprintf(fmt, args);
    va_end(args);
}
