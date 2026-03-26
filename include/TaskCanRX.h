#include <semphr.h>

typedef struct
{
    uint8_t id;
    uint32_t data;
} dataCan;

void TaskCanRx(void *pvParameters);
void get_CAN_Rx_Message(dataCan *msg);

extern SemaphoreHandle_t xCanRxSemaphore;