#include "store_forward.h"
#include <string.h>

static uint8_t buf[SF_MAX_SLOTS][SF_SLOT_SIZE];
static int head  = 0;
static int tail  = 0;
static int count = 0;

void sf_init(void)
{
    memset(buf, 0, sizeof(buf));
    head = tail = count = 0;
}

int sf_write(const uint8_t *packet)
{
    if (count >= SF_MAX_SLOTS) return SF_FULL;
    memcpy(buf[tail], packet, SF_SLOT_SIZE);
    tail = (tail + 1) % SF_MAX_SLOTS;
    count++;
    return SF_OK;
}

int sf_read(uint8_t *packet)
{
    if (count == 0) return SF_EMPTY;
    memcpy(packet, buf[head], SF_SLOT_SIZE);
    head = (head + 1) % SF_MAX_SLOTS;
    count--;
    return SF_OK;
}

int sf_pending(void)
{
    return count;
}
