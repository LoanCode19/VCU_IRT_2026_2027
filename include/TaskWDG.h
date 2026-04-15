#ifdef USE_WDG

#include "structs.h"
#ifndef TASK_WDG_H
#define TASK_WDG_H
extern WDGTimoutTable wdgTimeoutTable;

void TaskWDG(void *pvParameters);

#endif // TASK_WDG_H
#endif // USE_WDG