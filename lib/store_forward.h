#ifndef STORE_FORWARD_H
#define STORE_FORWARD_H

#include <stdint.h>
#include "rolling_buffer.h"

/*
 * Ring buffer backed by static RAM.
 * RAK4631 (nRF52840) has 256 KB RAM — 200 slots * 50 bytes = 10 KB.
 * NUCLEO-L073RZ (20 KB RAM) should use 20 slots max.
 */
#define SF_MAX_SLOTS   200
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