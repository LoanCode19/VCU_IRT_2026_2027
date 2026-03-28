#include <semphr.h>

void TaskCanRx(void *pvParameters);
void get_CAN_Rx_Message(dataCan *msg);
void GetNextCANMessage(dataCan *msg, int can, int fifo);

extern SemaphoreHandle_t xCanRxSemaphore;