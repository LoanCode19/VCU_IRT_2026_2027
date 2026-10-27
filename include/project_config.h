#ifndef PROJECT_CONFIG_H
#define PROJECT_CONFIG_H

// Project-specific configuration parameters
#define MAX_TASKS TASK_COUNT // une case par tache de TaskId (structs.h)

// Periode de TaskWDG : bien plus courte que l'IWDG de la carte (environ 500 ms), sinon reset en boucle
#define WDG_PERIOD pdMS_TO_TICKS(100)
#define IHM_TIMEOUT pdMS_TO_TICKS(100)
#define DS_TIMEOUT pdMS_TO_TICKS(100)
// Attente maximale de CanRx et CanTx : elles nourrissent le watchdog au moins a ce rythme,
// meme sans trafic. La perte du bus est geree par les timeouts des donnees, pas par le watchdog.
#define CAN_RX_TIMEOUT pdMS_TO_TICKS(100)
#define CAN_TX_TIMEOUT pdMS_TO_TICKS(100)

// Boucle de controle (R1, T3, appliquee au lot 5, revisable) : TRVP et Main font une passe toutes les
// 10 ms, quel que soit le trafic CAN. Apres une pause, la rampe n'avance que du temps de deux periodes.
#define CONTROL_PERIOD_MS 10U
#define CONTROL_MAX_ELAPSED_MS 20U

#define MAIN_WDG_TIMEOUT pdMS_TO_TICKS(500)
#define TRVP_WDG_TIMEOUT pdMS_TO_TICKS(500)
#define IHM_WDG_TIMEOUT pdMS_TO_TICKS(500)
#define CAN_RX_WDG_TIMEOUT pdMS_TO_TICKS(1000)
#define CAN_TX_WDG_TIMEOUT pdMS_TO_TICKS(500)
#define DS_WDG_TIMEOUT pdMS_TO_TICKS(500)
#define COOLING_WDG_TIMEOUT pdMS_TO_TICKS(500)

// Pedale d'accelerateur (S1) : plus de 10 points d'ecart entre les deux APPS = incoherence (T 11.8.8),
// confirmee si elle dure plus de 100 ms. Le couple revient ensuite quand les APPS sont coherents et que
// la pedale est sous 5 % (D2, decide le 27/10/2026, revisable).
#define APPS_MAX_DIFF_PERCENT 10.0f
#define APPS_FAULT_CONFIRM_MS 100U
#define APPS_FAULT_RELEASE_PERCENT 5.0f

// Delai de chaque donnee dans Main (S5, T2, decide le 27/10/2026, revisable ; a reprendre du DBC) :
// au-dela, la donnee est perimee et le couple coupe. Avec les 200 ms de TRVP pour les APPS, la chaine
// reste sous les 500 ms de T 11.9.4.
#define THROTTLE_CMD_TIMEOUT_MS 100
#define SPEED_TIMEOUT_MS 200
#define TEMP_TIMEOUT_MS 500
#define VOLTAGE_TIMEOUT_MS 500

// Traces des taches de la chaine de couple (TRVP, Main) : leur etat une fois par seconde, jamais a chaque
// passage (T17). La console SWV de la carte ne suit pas 100 passages par seconde.
#define TRACE_PERIOD_MS 1000

#endif // PROJECT_CONFIG_H