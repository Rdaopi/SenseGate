#pragma once
#include <stdint.h>
#include <stddef.h>

#define SG_HAL_OK      0
#define SG_HAL_ERROR  -1
#define SG_HAL_FULL   -2
#define SG_HAL_EMPTY  -3

typedef enum {
    HAL_ROLE_SLAVE  = 0,
    HAL_ROLE_MASTER = 1
} hal_role_t;

int        hal_init(void);
int        hal_flash_write(const uint8_t *packet, size_t len);
int        hal_flash_read(uint8_t *packet, size_t len);
int        hal_flash_pending(void);
int        hal_radio_rx(uint8_t *packet, size_t len);
int        hal_modem_send_sms(const uint8_t *payload, size_t len);
hal_role_t hal_get_role(void);
void       hal_sim_set_role(hal_role_t role);
