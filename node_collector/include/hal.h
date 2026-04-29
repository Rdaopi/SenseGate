#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stddef.h>
#include "payload_packer.h"
#include "rolling_buffer.h"

typedef struct {
    uint8_t  state;
    uint32_t pallet_id;
    uint16_t wrap_time;
    uint16_t wrap_transit_time;
    uint16_t pallet_rotations;
    uint8_t  program_number;
    uint16_t pallet_perimeter;
    uint32_t running_seconds;
    uint32_t alarm_seconds;
    uint32_t machine_timestamp;
} hal_sensor_data_t;

#define HAL_OK      0
#define HAL_ERROR  -1
#define HAL_FULL   -2
#define HAL_EMPTY  -3

int  hal_init(void);
int  hal_sensor_read(hal_sensor_data_t *data, uint16_t seq);
int  hal_flash_write(const uint8_t *packet, size_t len);
int  hal_flash_read(uint8_t *packet, size_t len);
int  hal_flash_pending(void);
int  hal_radio_tx(const uint8_t *packet, size_t len);
int  hal_crypto_get_key(uint8_t *key_out, size_t key_len);
void hal_crypto_make_nonce(uint8_t *nonce_out, uint8_t device_id, uint16_t seq);

#endif /* HAL_H */