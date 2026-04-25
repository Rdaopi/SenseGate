#ifndef ROLLING_BUFFER_H
#define ROLLING_BUFFER_H

#include <stdint.h>
#include "payload_packer.h"
#include "aes_ctr.h"

/* Each transmission sends current + previous = 38 byte */
#define TX_PACKET_SIZE (PAYLOAD_SIZE * 2)

/*
 * Keeps in memory the last transmitted encrypted packet.
 * On the next TX, it concatenates it with the current one.
 */
void rolling_buffer_init(void);

/*
 * Builds the 38-byte packet:
 *   [0..18]  = current ciphertext
 *   [19..37] = previous ciphertext (or zero on first startup)
 *
 * Updates the internal buffer with the current ciphertext.
 */
void rolling_buffer_build(const uint8_t *ciphertext,
                           uint8_t       *tx_packet);

#endif