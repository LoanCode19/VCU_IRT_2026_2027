#ifndef PROJECT_CONFIG_H
#define PROJECT_CONFIG_H

// Project-specific configuration parameters
#define MAX_TASKS TASK_COUNT // une case par tache de TaskId (structs.h)

#define WDG_TIMEOUT pdMS_TO_TICKS(1000)
#define MAIN_TIMOUT pdMS_TO_TICKS(100)
#define TRVP_TIMEOUT pdMS_TO_TICKS(100)
#define IHM_TIMEOUT pdMS_TO_TICKS(100)
#define CAN_RX_TIMEOUT portMAX_DELAY
#define CAN_TX_TIMEOUT portMAX_DELAY

#define MAIN_WDG_TIMEOUT pdMS_TO_TICKS(500)
#define TRVP_WDG_TIMEOUT pdMS_TO_TICKS(500)
#define IHM_WDG_TIMEOUT pdMS_TO_TICKS(500)
#define CAN_RX_WDG_TIMEOUT pdMS_TO_TICKS(1000)
#define CAN_TX_WDG_TIMEOUT pdMS_TO_TICKS(500)

#endif // PROJECT_CONFIG_H