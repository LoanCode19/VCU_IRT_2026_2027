#ifdef USE_WDG

#include "structs.h"

#ifndef TASK_WDG_H
#define TASK_WDG_H

extern WDGTimoutTable wdgTimeoutTable;

void TaskWDG(void *pvParameters);

typedef struct
{
    QueueHandle_t *xWDGQueue;
    TaskId taskId;

} WdgParams_t;

#endif // TASK_WDG_H
#endif // USE_WDG