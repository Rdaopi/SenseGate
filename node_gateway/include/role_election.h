#ifndef ROLE_ELECTION_H
#define ROLE_ELECTION_H

#include "hal.h"

/*
 * Role election — SenseGate
 *
 * Il ruolo viene eletto UNA SOLA VOLTA al boot
 * leggendo GPIO SIM_DETECT tramite HAL.
 *
 * Adesso: hal_get_role() ritorna un valore simulato
 * Dopo:   hal_get_role() legge il pin fisico SIM_DETECT
 *
 * Il ruolo non cambia durante l'esecuzione.
 * Per cambiarlo: rimuovi/inserisci SIM e riavvia il nodo.
 */

typedef enum {
    ROLE_UNSET  = -1,
    ROLE_SLAVE  =  0,
    ROLE_MASTER =  1
} node_role_t;

/*
 * Chiama questa funzione UNA SOLA VOLTA all'inizio del main.
 * Legge il GPIO, imposta il ruolo, stampa il risultato.
 */
node_role_t role_election_run(void);

/*
 * Ritorna il ruolo corrente (dopo che election_run e' stato chiamato).
 */
node_role_t role_election_get(void);

/*
 * Utility
 */
const char *role_to_string(node_role_t role);

#endif