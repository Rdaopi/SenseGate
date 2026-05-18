/* =============================================================
 * HAL HW — Siemens SIMATIC ET 200SP CPU 1510SP-1 PN
 * Communication: Modbus RTU via CM PtP RS485 module
 *
 * TIA Portal configuration required:
 *   1. Add CM PtP RS485 module to ET 200SP hardware config
 *   2. Enable Modbus RTU master/slave mode on CM PtP
 *   3. Baud rate: 9600 bps (confirm with Technowrapp)
 *   4. Parity: None, 8 data bits, 1 stop bit (8N1)
 *   5. Slave address: 1 (confirm with Technowrapp)
 *   6. Map PLC data blocks to holding registers 40001-40014:
 *      40001 = machine state code
 *      40002-3 = pallet_id (uint32 split high/low)
 *      40004 = wrap_time (seconds)
 *      40005 = wrap_transit_time (seconds)
 *      40006 = pallet_rotations
 *      40007 = program_number
 *      40008 = pallet_perimeter
 *      40009-10 = running_seconds (uint32 split high/low)
 *      40011-12 = alarm_seconds (uint32 split high/low)
 *      40013-14 = machine_timestamp Unix epoch (uint32 high/low)
 *
 * Physical connection:
 *   STM32L073 UART2 TX → MAX3485 DI
 *   STM32L073 UART2 RX → MAX3485 RO
 *   STM32L073 PA1      → MAX3485 DE+RE (direction control)
 *   MAX3485 A/B        → RS-485 cable → CM PtP RS485 terminal
 * ============================================================= */

#include "hal.h"
#include "aes_ctr.h"
#include "store_forward.h"
#include "rolling_buffer.h"
#include "modbus_sim.h"

#include <zephyr/modbus/modbus.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>

/* TODO: Confirm slave address with Technowrapp */
#define MODBUS_SLAVE_ADDR   1

/* TODO: Set to the UART device label for the RS-485 port on RAK4630.
 * On RAK4630: UART1 = "UART_1", UART2 = "UART_2" — confirm in devicetree. */
#define MODBUS_UART_DEV     "UART_1"

/* TODO: Confirm baud rate with Technowrapp (9600 or 19200) */
#define MODBUS_BAUD         9600

/* Modbus RTU timeout — 1.5× character time at 9600 bps ≈ 2 ms; use 50 ms for margin */
#define MODBUS_TIMEOUT_MS   50

/* AES-128 key — must match hal_sim.c / firmware exactly */
static const uint8_t HW_KEY[AES_KEY_SIZE] = {
    0x2B, 0x7E, 0x15, 0x16,
    0x28, 0xAE, 0xD2, 0xA6,
    0xAB, 0xF7, 0x15, 0x88,
    0x09, 0xCF, 0x4F, 0x3C
};

/* Modbus client interface handle */
static int modbus_iface = -1;


/* ── hal_init ────────────────────────────────────────────────────────────────*/

int hal_init(void)
{
    printk("[HAL HW] Initializing — Modbus RTU master on %s\n", MODBUS_UART_DEV);

    /* TODO: Replace "UART_1" with the actual Zephyr device node label.
     * Check your board's .dts or .overlay file: look for uart@... aliases. */
    struct modbus_iface_param params = {
        .mode           = MODBUS_MODE_RTU,
        .rx_timeout     = K_MSEC(MODBUS_TIMEOUT_MS),
        .serial = {
            .baud       = MODBUS_BAUD,
            .parity     = UART_CFG_PARITY_NONE,
            /* TODO: verify flow control — DE/RE on PA1 is handled by the
             * Zephyr RS-485 driver if you set rs485_de_active_high in DTS. */
        },
    };

    modbus_iface = modbus_iface_get_by_name(MODBUS_UART_DEV);
    if (modbus_iface < 0) {
        printk("[HAL HW] ERROR: Modbus iface not found: %s\n", MODBUS_UART_DEV);
        return HAL_ERROR;
    }

    int rc = modbus_init_client(modbus_iface, params);
    if (rc != 0) {
        printk("[HAL HW] ERROR: modbus_init_client failed: %d\n", rc);
        return HAL_ERROR;
    }

    if (aes_ctr_init(HW_KEY, AES_KEY_SIZE) != 0) {
        printk("[HAL HW] ERROR: AES init failed\n");
        return HAL_ERROR;
    }

    rolling_buffer_init();
    sf_init();

    printk("[HAL HW] Ready — slave addr=%d baud=%d\n", MODBUS_SLAVE_ADDR, MODBUS_BAUD);
    return HAL_OK;
}


/* ── hal_sensor_read ─────────────────────────────────────────────────────────*/

int hal_sensor_read(hal_sensor_data_t *data, uint16_t seq)
{
    uint16_t regs[MB_NUM_REGS] = {0};

    /* FC03: Read MB_NUM_REGS holding registers starting at address 0 (40001) */
    int rc = modbus_read_holding_regs(
        modbus_iface,
        MODBUS_SLAVE_ADDR,
        0,          /* start address — 40001 in PLC = address 0 in Modbus frame */
        regs,
        MB_NUM_REGS
    );

    if (rc != 0) {
        printk("[HAL HW] ERROR: Modbus FC03 failed (rc=%d) seq=%u\n", rc, seq);
        return HAL_ERROR;
    }

    /* Unpack registers — same mapping as modbus_sim.h */
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
        printk("[HAL HW] WARNING: machine OFFLINE (seq=%u)\n", seq);
    }

    return HAL_OK;
}


/* ── hal_flash_write ─────────────────────────────────────────────────────────*/

int hal_flash_write(const uint8_t *packet, size_t len)
{
    if (len != SF_SLOT_SIZE) return HAL_ERROR;
    return (sf_write(packet) == SF_OK) ? HAL_OK : HAL_FULL;
}


/* ── hal_flash_read ──────────────────────────────────────────────────────────*/

int hal_flash_read(uint8_t *packet, size_t len)
{
    if (len != SF_SLOT_SIZE) return HAL_ERROR;
    return (sf_read(packet) == SF_OK) ? HAL_OK : HAL_EMPTY;
}


/* ── hal_flash_pending ───────────────────────────────────────────────────────*/

int hal_flash_pending(void)
{
    return sf_pending();
}


/* ── hal_radio_tx ────────────────────────────────────────────────────────────*/

int hal_radio_tx(const uint8_t *packet, size_t len)
{
    /* TODO: Replace printk stub with real LoRa TX once RAK4630 radio driver
     * is confirmed. Use: lora_send(&lora_dev, packet, len, false)
     * Device node: &lora0 in RAK4630 DTS. */
    printk("[HAL HW] LoRa TX %d bytes: ", (int)len);
    for (size_t i = 0; i < len; i++) {
        printk("%02X", packet[i]);
    }
    printk("\n");
    return HAL_OK;
}


/* ── hal_crypto_get_key ──────────────────────────────────────────────────────*/

int hal_crypto_get_key(uint8_t *key_out, size_t key_len)
{
    /* TODO: Replace hardcoded key with ATECC608B secure-element read:
     *   atcab_read_zone(ATCA_ZONE_DATA, KEY_SLOT, 0, 0, key_out, key_len)
     * For initial bring-up the hardcoded key is used. */
    if (key_len != AES_KEY_SIZE) return HAL_ERROR;
    memcpy(key_out, HW_KEY, AES_KEY_SIZE);
    return HAL_OK;
}


/* ── hal_crypto_make_nonce ───────────────────────────────────────────────────*/

void hal_crypto_make_nonce(uint8_t *nonce_out,
                           uint8_t  device_id,
                           uint16_t seq)
{
    /* Layout: [device_id 1B][seq high 1B][seq low 1B][zeros 13B]
     * Must stay in sync with aes_decrypt.py _build_nonce(). */
    memset(nonce_out, 0, AES_NONCE_SIZE);
    nonce_out[0] = device_id;
    nonce_out[1] = (seq >> 8) & 0xFF;
    nonce_out[2] = seq & 0xFF;
}
