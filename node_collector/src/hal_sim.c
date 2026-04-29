#include "hal.h"
#include "aes_ctr.h"
#include "store_forward.h"
#include "rolling_buffer.h"
#include "modbus_sim.h"
#include <string.h>
#include <zephyr/sys/printk.h>

static const uint8_t SIM_KEY[AES_KEY_SIZE] = {
    0x2B, 0x7E, 0x15, 0x16,
    0x28, 0xAE, 0xD2, 0xA6,
    0xAB, 0xF7, 0x15, 0x88,
    0x09, 0xCF, 0x4F, 0x3C
};

int hal_init(void)
{
    printk("[HAL COLLECTOR SIM] Initializing\n");

    if (aes_ctr_init(SIM_KEY, AES_KEY_SIZE) != 0) {
        printk("[HAL COLLECTOR SIM] ERROR: AES init failed\n");
        return HAL_ERROR;
    }

    rolling_buffer_init();
    sf_init();
    modbus_sim_init();

    printk("[HAL COLLECTOR SIM] Ready\n");
    return HAL_OK;
}

int hal_sensor_read(hal_sensor_data_t *data, uint16_t seq)
{
    /* Update simulated registers before reading */
    modbus_sim_tick(seq);

    uint16_t regs[MB_NUM_REGS];
    if (modbus_sim_read_registers(regs, MB_NUM_REGS) != 0) {
        printk("[HAL COLLECTOR SIM] ERROR: Modbus read failed\n");
        return HAL_ERROR;
    }

    data->state = (uint8_t)regs[MB_REG_STATE];

    data->pallet_id = ((uint32_t)regs[MB_REG_PALLET_HIGH] << 16)
                    | regs[MB_REG_PALLET_LOW];

    data->wrap_time         = regs[MB_REG_WRAP_TIME];
    data->wrap_transit_time = regs[MB_REG_WRAP_TRANSIT];
    data->pallet_rotations  = regs[MB_REG_ROTATIONS];
    data->program_number    = (uint8_t)regs[MB_REG_PROGRAM];
    data->pallet_perimeter  = regs[MB_REG_PERIMETER];

    data->running_seconds = ((uint32_t)regs[MB_REG_RUN_HIGH] << 16)
                          | regs[MB_REG_RUN_LOW];

    data->alarm_seconds   = ((uint32_t)regs[MB_REG_ALARM_HIGH] << 16)
                          | regs[MB_REG_ALARM_LOW];

    data->machine_timestamp = ((uint32_t)regs[MB_REG_TS_HIGH] << 16)
                            | regs[MB_REG_TS_LOW];

    if (data->state == 99) {
        printk("[HAL COLLECTOR SIM] WARNING: machine OFFLINE\n");
    }

    return HAL_OK;
}

int hal_flash_write(const uint8_t *packet, size_t len)
{
    if (len != SF_SLOT_SIZE) return HAL_ERROR;
    return (sf_write(packet) == SF_OK) ? HAL_OK : HAL_FULL;
}

int hal_flash_read(uint8_t *packet, size_t len)
{
    if (len != SF_SLOT_SIZE) return HAL_ERROR;
    return (sf_read(packet) == SF_OK) ? HAL_OK : HAL_EMPTY;
}

int hal_flash_pending(void)
{
    return sf_pending();
}

int hal_radio_tx(const uint8_t *packet, size_t len)
{
    /*
     * Simulates LoRa TX to gateway.
     * Later: lora_send(&lora_dev, packet, len, false)
     */
    printk("[HAL COLLECTOR SIM] LoRa TX %d bytes: ", len);
    for (size_t i = 0; i < len; i++) {
        printk("%02X", packet[i]);
    }
    printk("\n");
    return HAL_OK;
}

int hal_crypto_get_key(uint8_t *key_out, size_t key_len)
{
    /*
     * Returns hardcoded key.
     * Later: atcab_read_zone() on ATECC608B via I2C
     */
    if (key_len != AES_KEY_SIZE) return HAL_ERROR;
    memcpy(key_out, SIM_KEY, AES_KEY_SIZE);
    return HAL_OK;
}

void hal_crypto_make_nonce(uint8_t *nonce_out,
                           uint8_t  device_id,
                           uint16_t seq)
{
    memset(nonce_out, 0, AES_NONCE_SIZE);
    nonce_out[0] = device_id;
    nonce_out[1] = (seq >> 8) & 0xFF;
    nonce_out[2] = seq & 0xFF;
}