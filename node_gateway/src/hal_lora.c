/*
 * hal_lora.c — gateway HAL, real hardware implementation
 *
 * Drop-in replacement for hal_sim.c targeting actual Cortex-M hardware.
 * To activate:
 *   1. In CMakeLists.txt replace hal_sim.c with hal_lora.c
 *   2. In prj.conf uncomment CONFIG_LORA=y
 *   3. Add a board overlay with the lora0 DTS alias
 *
 * In QEMU the LoRa device is never ready, so hal_radio_rx() reads a
 * 50-byte hex string from stdin (written by sim_bridge.py) — this is
 * the simulation path that wires the two QEMU nodes together.
 *
 * Stdin packet format (written by sim_bridge.py):
 *   "LORA_RX <HEX>\n"   where HEX is SF_SLOT_SIZE*2 hex chars (100 chars)
 */

#include "hal.h"
#include "store_forward.h"
#include "rolling_buffer.h"
#include "lora_link.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#if DT_HAS_ALIAS(lora0)
static const struct device *lora_dev = DEVICE_DT_GET(LORA_NODE);
#else
static const struct device *lora_dev;
#endif

static bool lora_ready;
static hal_role_t sim_role = HAL_ROLE_MASTER;

/* ── helpers ────────────────────────────────────────────────────────────── */

static uint8_t hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static int hex_to_bytes(const char *hex, uint8_t *out, size_t out_len)
{
    for (size_t i = 0; i < out_len; i++) {
        out[i] = (hex_nibble(hex[i * 2]) << 4) | hex_nibble(hex[i * 2 + 1]);
    }
    return 0;
}

/* ── init ───────────────────────────────────────────────────────────────── */

int hal_init(void)
{
    printk("[HAL GW HW] Initializing gateway\n");

    sf_init();
    rolling_buffer_init();

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
            .tx           = false,   /* gateway is RX */
        };
        if (lora_config(lora_dev, &cfg) != 0) {
            printk("[HAL GW HW] ERROR: lora_config failed\n");
            lora_ready = false;
        } else {
            printk("[HAL GW HW] LoRa configured @ %d Hz SF%d (RX mode)\n",
                   LORA_FREQUENCY, 10);
        }
    } else {
        printk("[HAL GW SIM] LoRa device not ready (QEMU?) — RX reads from stdin\n");
    }

    printk("[HAL GW HW] Ready. Role: %s\n",
           sim_role == HAL_ROLE_MASTER ? "MASTER" : "SLAVE");
    return HAL_OK;
}

/* ── flash (store-and-forward) ──────────────────────────────────────────── */

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

/* ── radio RX ───────────────────────────────────────────────────────────── */

int hal_radio_rx(uint8_t *packet, size_t len)
{
    if (len < SF_SLOT_SIZE) return HAL_ERROR;

    if (lora_ready) {
        int16_t rssi;
        int8_t  snr;
        int ret = lora_recv(lora_dev, packet, len,
                            K_MSEC(LORA_RX_TIMEOUT_MS), &rssi, &snr);
        if (ret < 0) {
            printk("[HAL GW HW] lora_recv timeout or error (%d)\n", ret);
            return HAL_ERROR;
        }
        printk("[HAL GW HW] LoRa RX %d bytes  RSSI=%d dBm  SNR=%d dB\n",
               ret, rssi, snr);
        return HAL_OK;
    }

    /*
     * QEMU simulation path.
     * sim_bridge.py writes "LORA_RX <100-char-hex>\n" to gateway stdin
     * after it captures each LoRa TX line from the collector QEMU process.
     */
    char line[128];
    if (fgets(line, sizeof(line), stdin) == NULL) {
        return HAL_ERROR;
    }

    /* Expect "LORA_RX AABB...CC\n" */
    if (strncmp(line, "LORA_RX ", 8) != 0) {
        printk("[HAL GW SIM] Unexpected stdin line: %s", line);
        return HAL_ERROR;
    }

    const char *hex = line + 8;
    size_t hex_len = strlen(hex);
    /* Strip trailing newline */
    while (hex_len > 0 && (hex[hex_len-1] == '\n' || hex[hex_len-1] == '\r')) {
        hex_len--;
    }

    if (hex_len != SF_SLOT_SIZE * 2) {
        printk("[HAL GW SIM] Bad hex length %d (expected %d)\n",
               (int)hex_len, (int)(SF_SLOT_SIZE * 2));
        return HAL_ERROR;
    }

    hex_to_bytes(hex, packet, SF_SLOT_SIZE);
    printk("[HAL GW SIM] LoRa RX simulated via stdin (%d bytes)\n", (int)SF_SLOT_SIZE);
    return HAL_OK;
}

/* ── NB-IoT modem ───────────────────────────────────────────────────────── */

int hal_modem_send_sms(const uint8_t *payload, size_t len)
{
    /*
     * Real implementation: AT+CMGF=0, AT+CMGS="+destination", hex + CTRL+Z.
     * Simulation: print hex so sim_bridge.py can pick it up and POST /sms.
     */
    printk("[HAL GW SIM] NB-IoT SMS (%d bytes): ", (int)len);
    for (size_t i = 0; i < len; i++) {
        printk("%02X", payload[i]);
    }
    printk("\n");
    return HAL_OK;
}

/* ── role ───────────────────────────────────────────────────────────────── */

hal_role_t hal_get_role(void)
{
    return sim_role;
}

void hal_sim_set_role(hal_role_t role)
{
    sim_role = role;
}
