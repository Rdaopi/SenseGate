#ifndef STORE_FORWARD_H
#define STORE_FORWARD_H

#include <stdint.h>
#include "rolling_buffer.h"

/*
 * Simula il W25Q32 NOR flash con un array statico in RAM.
 * In produzione: stesse funzioni, driver SPI flash sotto.
 *
 * 20 slot = 1000 bytes — adatto a NUCLEO-L073RZ (20KB RAM).
 * Aumentare a 200 quando si usa il flash NOR esterno W25Q32.
 */
#define SF_MAX_SLOTS   20
#define SF_SLOT_SIZE   TX_PACKET_SIZE   /* 38 byte */

/* Stati possibili del buffer */
#define SF_OK          0
#define SF_FULL       -1
#define SF_EMPTY      -2

void sf_init(void);
int  sf_write(const uint8_t *tx_packet);   /* ritorna SF_OK o SF_FULL */
int  sf_read(uint8_t *tx_packet);          /* ritorna SF_OK o SF_EMPTY */
int  sf_pending(void);                     /* numero pacchetti in attesa */
void sf_stats(void);                       /* stampa stato buffer */

#endif