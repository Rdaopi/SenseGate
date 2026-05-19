/*
 * hal_lora.c — collector HAL, real hardware implementation
 *
 * Drop-in replacement for hal_sim.c targeting actual Cortex-M hardware.
 * To activate:
 *   1. In CMakeLists.txt replace hal_sim.c with hal_lora.c
 *   2. In prj.conf uncomment CONFIG_LORA=y (and CONFIG_MODBUS=y for sensor)
 *   3. Add a board overlay with the lora0 DTS alias
 *
 * In QEMU (qemu_cortex_m3) the LoRa device is never ready, so
 * hal_radio_tx() prints the hex to console exactly as hal_sim.c does —
 * no functional regression in simulation.
 */

#include "hal.h"
#include "aes_ctr.h"
#include "store_forward.h"
#include "rolling_buffer.h"
#include "lora_link.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/sys/printk.h>
#include <string.h>

static const uint8_t HW_KEY[AES_KEY_SIZE] = {
    0x2B, 0x7E, 0x15, 0x16,
    0x28, 0xAE, 0xD2, 0xA6,
    0xAB, 0xF7, 0x15, 0x88,
    0x09, 0xCF, 0x4F, 0x3C
};

#if DT_HAS_ALIAS(lora0)
static const struct device *lora_dev = DEVICE_DT_GET(LORA_NODE);
#else
static const struct device *lora_dev;   /* NULL — no LoRa node in DTS */
#endif

static bool lora_ready;

/* ── init ──────────────────────────────────────────────────────────────── */

int hal_init(void)
{
    printk("[HAL COLLECTOR HW] Initializing\n");

    if (aes_ctr_init(HW_KEY, AES_KEY_SIZE) != 0) {
        printk("[HAL COLLECTOR HW] ERROR: AES init failed\n");
        return HAL_ERROR;
    }

    rolling_buffer_init();
    sf_init();

#if DT_HAS_ALIAS(lora0)
    lora_ready = device_is_ready(lora_dev);
#else
    lora_ready = false;
#endif

    if (lora_ready) {
        struct lora_modem_config cfg = {
            .frequency    = LORA_FREQUENCY,
            .bandwidth    = LORA_BANDWIDTH,
            .datarate     = LORA_DATARATE,
            .coding_rate  = LORA_CODING_RATE,
            .preamble_len = LORA_PREAMBLE_LEN,
            .tx_power     = LORA_TX_POWER,
            .tx           = true,
        };
        if (lora_config(lora_dev, &cfg) != 0) {
            printk("[HAL COLLECTOR HW] ERROR: lora_config failed\n");
            lora_ready = false;
        } else {
            printk("[HAL COLLECTOR HW] LoRa configured @ %d Hz SF%d\n",
                   LORA_FREQUENCY, 10);
        }
    } else {
        printk("[HAL COLLECTOR HW] LoRa device not ready (QEMU?) — TX will print hex\n");
    }

    printk("[HAL COLLECTOR HW] Ready\n");
    return HAL_OK;
}

/* ── flash (store-and-forward) ─────────────────────────────────────────── */

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

/* ── radio TX ──────────────────────────────────────────────────────────── */

int hal_radio_tx(const uint8_t *packet, size_t len)
{
    if (lora_ready) {
        int ret = lora_send(lora_dev, (uint8_t *)packet, len);
        if (ret != 0) {
            printk("[HAL COLLECTOR HW] ERROR: lora_send failed (%d)\n", ret);
            return HAL_ERROR;
        }
        printk("[HAL COLLECTOR HW] LoRa TX %d bytes sent\n", (int)len);
        return HAL_OK;
    }

    /* QEMU fallback: same format as hal_sim.c so sim_bridge.py still works */
    printk("[HAL COLLECTOR SIM] LoRa TX %d bytes: ", (int)len);
    for (size_t i = 0; i < len; i++) {
        printk("%02X", packet[i]);
    }
    printk("\n");
    return HAL_OK;
}

/* ── crypto ────────────────────────────────────────────────────────────── */

int hal_crypto_get_key(uint8_t *key_out, size_t key_len)
{
    /*
     * On real hardware: atcab_read_zone(ATCA_ZONE_DATA, KEY_SLOT, ...) via I2C.
     * Returns the hardcoded key until ATECC608B driver is wired.
     */
    if (key_len != AES_KEY_SIZE) return HAL_ERROR;
    memcpy(key_out, HW_KEY, AES_KEY_SIZE);
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

/* ── sensor (Modbus RTU) ────────────────────────────────────────────────
 * hal_sensor_read() is NOT implemented here — use modbus_sim.c in QEMU
 * or hal_hw.c (Modbus RTU master) on real hardware.
 * Provide a weak stub so the linker is satisfied when this file is used
 * standalone in QEMU without hal_hw.c.
 */
__attribute__((weak))
int hal_sensor_read(hal_sensor_data_t *data, uint16_t seq)
{
    (void)data; (void)seq;
    printk("[HAL COLLECTOR HW] hal_sensor_read: no implementation linked\n");
    return HAL_ERROR;
}
