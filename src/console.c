/*
 * FreeRTOS V202107.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

/*-----------------------------------------------------------
 * Example console I/O wrappers.
 *----------------------------------------------------------*/

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "console.h"

// R7 : un mutex, sur la carte comme sur PC, pour que deux taches n'ecrivent jamais en meme temps. Avant le
// demarrage du scheduler, une seule tache tourne : pas de mutex. Jamais d'appel depuis une interruption (attente).
static SemaphoreHandle_t sConsoleMutex = NULL;
static StaticSemaphore_t sConsoleMutexBuffer;

void console_init(void)
{
    if (sConsoleMutex == NULL) // appelee par MainApp() : un deuxieme appel est sans effet
    {
        sConsoleMutex = xSemaphoreCreateMutexStatic(&sConsoleMutexBuffer);
    }
}

void console_print(const char *fmt, ...)
{
    bool locked = (sConsoleMutex != NULL) && (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) &&
                  (xSemaphoreTake(sConsoleMutex, portMAX_DELAY) == pdTRUE);

    va_list vargs;
    va_start(vargs, fmt);
    vprintf(fmt, vargs);
    va_end(vargs);

    if (locked)
    {
        xSemaphoreGive(sConsoleMutex);
    }
}
