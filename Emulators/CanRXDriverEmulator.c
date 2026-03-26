
#ifdef LINUX

#include "FreeRTOS.h"
#include "queue.h"

#include "structs.h"

#include "console.h"
#include "TaskCanRX.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static FILE *file = NULL;

/**
 * @brief Initialise la lecture du CSV
 */
int CAN_Replay_Init(const char *filename)
{
    file = fopen(filename, "r");
    if (!file)
    {
        console_print("no file found");
        return 0;
    }

    // Skip header (id;data)
    char buffer[128];
    fgets(buffer, sizeof(buffer), file);

    return 1;
}

/**
 * @brief Lit le prochain message depuis le CSV
 */
int CAN_ReadNext(dataCan *msg)
{
    if (!file || !msg)
        return 0;

    char line[128];

    while (1)
    {
        // lire une ligne
        if (fgets(line, sizeof(line), file) == NULL)
        {
            // 👉 Si EOF → on s'arrête
            if (feof(file))
            {
                return 0;
            }

            // 👉 erreur de lecture
            return 0;
        }

        // trouver le séparateur ;
        char *sep = strchr(line, ';');
        if (!sep)
            continue;

        *sep = '\0';

        // parse ID (hex accepté grâce à base 0)
        unsigned long id = strtoul(line, NULL, 0);

        // parse data
        int data = strtol(sep + 1, NULL, 10);

        // sécurité
        if (id > 0xFF)
        {
            console_print("Invalid ID: %lu\n", id);
            continue;
        }

        msg->id = (uint8_t)id;
        msg->data = data;

        return 1;
    }
}

/**
 * @brief Ferme le fichier
 */
void CAN_Replay_Close(void)
{
    if (file)
    {
        fclose(file);
        file = NULL;
    }
}
#endif