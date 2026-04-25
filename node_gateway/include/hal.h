#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stddef.h>
#include "rolling_buffer.h"

#define HAL_OK      0
#define HAL_ERROR  -1
#define HAL_FULL   -2
#define HAL_EMPTY  -3

typedef enum {
    HAL_ROLE_SLAVE  = 0,
    HAL_ROLE_MASTER = 1
} hal_role_t;

int        hal_init(void);
int        hal_flash_write(const uint8_t *packet, size_t len);
int        hal_flash_read(uint8_t *packet, size_t len);
int        hal_flash_pending(void);
int        hal_radio_rx(uint8_t *packet, size_t len);  /* riceve da collector */
int        hal_modem_send_sms(const uint8_t *payload, size_t len);
hal_role_t hal_get_role(void);
#endif