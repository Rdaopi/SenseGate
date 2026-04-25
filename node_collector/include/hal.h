#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stddef.h>
#include "payload_packer.h"
#include "rolling_buffer.h"

typedef struct {
    float    temperature;
    float    humidity;
    float    vibration;
    float    pressure;
    uint32_t plc_cycles;
    uint16_t plc_hours;
    uint8_t  plc_status;
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
void hal_crypto_make_nonce(uint8_t *nonce_out,
                           uint8_t device_id,
                           uint16_t seq);
#endif