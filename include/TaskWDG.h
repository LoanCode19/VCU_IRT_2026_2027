#ifdef USE_WDG

#include "structs.h"

#ifndef TASK_WDG_H
#define TASK_WDG_H

extern WDGTimoutTable wdgTimeoutTable;

// Parametres de TaskWDG : la queue de TaskIHM, ou part la LED de vie (T24). Les signes de vie arrivent par la table
// wdgLastFeed (utils.h).
typedef struct
{
    QueueHandle_t *xIHMQueue;
} TaskWDGParams_t;

void TaskWDG(void *pvParameters);

#endif // TASK_WDG_H
#endif // USE_WDG