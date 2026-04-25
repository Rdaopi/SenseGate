#include "store_forward.h"
#include <string.h>
#include <zephyr/sys/printk.h>

/*
 * Ring buffer (FIFO) — simula il flash NOR.
 * head = prossimo slot da leggere
 * tail = prossimo slot da scrivere
 */
static uint8_t  buffer[SF_MAX_SLOTS][SF_SLOT_SIZE];
static int      head  = 0;
static int      tail  = 0;
static int      count = 0;

void sf_init(void)
{
    memset(buffer, 0, sizeof(buffer));
    head  = 0;
    tail  = 0;
    count = 0;
}

int sf_write(const uint8_t *tx_packet)
{
    if (count >= SF_MAX_SLOTS) {
        return SF_FULL;
    }
    memcpy(buffer[tail], tx_packet, SF_SLOT_SIZE);
    tail = (tail + 1) % SF_MAX_SLOTS;
    count++;
    return SF_OK;
}

int sf_read(uint8_t *tx_packet)
{
    if (count == 0) {
        return SF_EMPTY;
    }
    memcpy(tx_packet, buffer[head], SF_SLOT_SIZE);
    head = (head + 1) % SF_MAX_SLOTS;
    count--;
    return SF_OK;
}

int sf_pending(void)
{
    return count;
}

void sf_stats(void)
{
    printk("  Buffer: %d/%d slots used (%.1f%%)\n",
           count, SF_MAX_SLOTS,
           (double)count / SF_MAX_SLOTS * 100.0);
}