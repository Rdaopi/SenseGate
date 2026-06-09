#pragma once
#include <stdint.h>

#define PAYLOAD_SIZE   25
#define SF_SLOT_SIZE   (PAYLOAD_SIZE * 2)   /* 50 bytes: current + previous */
#define SF_MAX_SLOTS   200

#define SF_OK    0
#define SF_FULL -1
#define SF_EMPTY -2

void sf_init(void);
int  sf_write(const uint8_t *packet);
int  sf_read(uint8_t *packet);
int  sf_pending(void);
