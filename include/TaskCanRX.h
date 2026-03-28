#include <semphr.h>

void TaskCanRx(void *pvParameters);

extern SemaphoreHandle_t xCanRxSemaphore;