#include "hal.h"
#include "aes_ctr.h"
#include "store_forward.h"
#include "rolling_buffer.h"
#include "modbus_sim.h"
#include <string.h>
#include <zephyr/sys/printk.h>

static const uint8_t SIM_KEY[AES_KEY_SIZE] = {
    0x2B,0x7E,0x15,0x16,
    0x28,0xAE,0xD2,0xA6,
    0xAB,0xF7,0x15,0x88,
    0x09,0xCF,0x4F,0x3C
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
    modbus_sim_tick(seq);

    uint16_t regs[MB_NUM_REGS];
    if (modbus_sim_read_registers(regs, MB_NUM_REGS) != 0) {
        printk("[HAL COLLECTOR SIM] ERROR: Modbus read failed\n");
        return HAL_ERROR;
    }

    data->temperature = regs[MB_REG_TEMP]     / 10.0f;
    data->humidity    = regs[MB_REG_HUMIDITY]  / 2.0f;
    data->vibration   = regs[MB_REG_VIBRATION] / 64.0f;

    uint32_t pres_raw = ((uint32_t)regs[MB_REG_PRES_HIGH] << 16)
                       | regs[MB_REG_PRES_LOW];
    data->pressure    = (pres_raw / 16.0f) + 300.0f;

    data->plc_cycles  = ((uint32_t)regs[MB_REG_CYCLES_HIGH] << 16)
                       | regs[MB_REG_CYCLES_LOW];
    data->plc_hours   = regs[MB_REG_HOURS];
    data->plc_status  = (uint8_t)regs[MB_REG_STATUS];

    if (data->plc_status & 0x02) {
        printk("[HAL COLLECTOR SIM] WARNING: PLC alarm\n");
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
     * Simula TX LoRa verso il gateway.
     * Dopo: lora_send(&lora_dev, packet, len, false)
     */
    printk("[HAL COLLECTOR SIM] LoRa TX %d bytes -> ", len);
    for (size_t i = 0; i < len; i++) {
        printk("%02X ", packet[i]);
    }
    printk("\n");
    return HAL_OK;
}

int hal_crypto_get_key(uint8_t *key_out, size_t key_len)
{
    /*
     * Ritorna chiave hardcodata.
     * Dopo: atcab_read_zone() su ATECC608B via I2C
     */
    if (key_len != AES_KEY_SIZE) return HAL_ERROR;
    memcpy(key_out, SIM_KEY, AES_KEY_SIZE);
    return HAL_OK;
}

void hal_crypto_make_nonce(uint8_t *nonce_out,
                           uint8_t device_id,
                           uint16_t seq)
{
    memset(nonce_out, 0, AES_NONCE_SIZE);
    nonce_out[0] = device_id;
    nonce_out[1] = (seq >> 8) & 0xFF;
    nonce_out[2] = seq & 0xFF;
}