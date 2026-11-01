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

// Boucle de controle (R1, T3, decide le 27/10/2026, revisable) : TRVP et Main font une passe toutes les
// 10 ms, quel que soit le trafic CAN. Apres une pause, la rampe n'avance que du temps de deux periodes.
#define CONTROL_PERIOD_MS 10U
#define CONTROL_MAX_ELAPSED_MS 20U

// Queues de TRVP et de Main (T16, decide le 27/10/2026, revisable) : videes toutes les 10 ms, elles gardent
// de la marge pour des capteurs deux fois plus rapides que prevu (12 messages par periode vers TRVP).
// Queue pleine : le plus ancien message laisse sa place et la perte est comptee (T13).
#define TRVP_QUEUE_LENGTH 20U
#define MAIN_QUEUE_LENGTH 20U

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

// Etat du vehicule (trame provisoire 0x012 toutes les 10 ms, T19, decide le 27/10/2026, revisable) : au-dela de ce
// delai, la trame est perdue. Sortie du R2D, voyant defaut, et le frein compte comme appuye (T 11.9.4 : 500 ms au
// plus).
#define VEHICLE_STATE_TIMEOUT_MS 100

// Ready-to-Drive (S3, T8 revise le 27/10/2026, revisable) : son pendant 2 s a l'entree (EV 4.12.1 : de 1 a 3 s)
#define R2D_BUZZER_MS 2000U

// Vitesse des roues (D7, decide le 23/10/2026) : m/s dans les calculs, mm/s dans les entiers. Valeurs
// PROVISOIRES, a remplacer par les mesures sur la voiture ; seule WheelRawToMmS() les utilise.
#define WHEEL_SPEED_PROVISIONAL 1 // a retirer quand les deux valeurs ci-dessous sont mesurees (test D7a)
#define WHEEL_SENSOR_RAW_IS_RPM 1 // hypothese : le capteur donne des tr/min de roue
#define WHEEL_RADIUS_M 0.20f      // hypothese : rayon sous charge, a mesurer
// Ecart maximal entre deux roues pour les moyenner (T18, decide le 27/10/2026, revisable ; a regler en roulant)
#define WHEEL_MAX_DIFF_MMS 1000

// Traces des taches de la chaine de couple (TRVP, Main) : leur etat une fois par seconde, jamais a chaque
// passage (T17, decide le 27/10/2026, revisable). La console SWV de la carte ne suit pas 100 passages par
// seconde.
#define TRACE_PERIOD_MS 1000

#endif // PROJECT_CONFIG_H