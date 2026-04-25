#include "rolling_buffer.h"
#include <string.h>

/* Internal Buffer — last transmitted ciphertext */
static uint8_t previous[PAYLOAD_SIZE];
static int     has_previous = 0;

void rolling_buffer_init(void)
{
    memset(previous, 0, PAYLOAD_SIZE);
    has_previous = 0;
}

void rolling_buffer_build(const uint8_t *ciphertext,
                           uint8_t       *tx_packet)
{
    /* First slot: current packet */
    memcpy(tx_packet, ciphertext, PAYLOAD_SIZE);

    /* Second slot: previous packet (zero on first startup) */
    memcpy(tx_packet + PAYLOAD_SIZE, previous, PAYLOAD_SIZE);

    /* Update buffer */
    memcpy(previous, ciphertext, PAYLOAD_SIZE);
    has_previous = 1;
}

int rolling_buffer_has_previous(void)
{
    return has_previous;
}