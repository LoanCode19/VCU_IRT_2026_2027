#ifndef PROJECT_CONFIG_H
#define PROJECT_CONFIG_H

// Project-specific configuration parameters
#define MAX_TASKS TASK_COUNT // une case par tache de TaskId (structs.h)

// Periode de TaskWDG : bien plus courte que l'IWDG de la carte (environ 500 ms), sinon reset en boucle
#define WDG_PERIOD pdMS_TO_TICKS(100)
#define MAIN_TIMOUT pdMS_TO_TICKS(100)
#define TRVP_TIMEOUT pdMS_TO_TICKS(100)
#define IHM_TIMEOUT pdMS_TO_TICKS(100)
#define DS_TIMEOUT pdMS_TO_TICKS(100)
// Attente maximale de CanRx et CanTx : elles nourrissent le watchdog au moins a ce rythme,
// meme sans trafic. La perte du bus est geree par les timeouts des donnees, pas par le watchdog.
#define CAN_RX_TIMEOUT pdMS_TO_TICKS(100)
#define CAN_TX_TIMEOUT pdMS_TO_TICKS(100)

#define MAIN_WDG_TIMEOUT pdMS_TO_TICKS(500)
#define TRVP_WDG_TIMEOUT pdMS_TO_TICKS(500)
#define IHM_WDG_TIMEOUT pdMS_TO_TICKS(500)
#define CAN_RX_WDG_TIMEOUT pdMS_TO_TICKS(1000)
#define CAN_TX_WDG_TIMEOUT pdMS_TO_TICKS(500)
#define DS_WDG_TIMEOUT pdMS_TO_TICKS(500)
#define COOLING_WDG_TIMEOUT pdMS_TO_TICKS(500)

#endif // PROJECT_CONFIG_H