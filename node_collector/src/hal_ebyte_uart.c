/*
 * hal_ebyte_uart.c — collector HAL for Ebyte E22-900T22S (UART LoRa module)
 *
 * The T22S is NOT a raw SX1262: it runs Ebyte's own firmware and is driven
 * over UART in transparent mode. Zephyr's LoRa driver (CONFIG_LORA) does not
 * apply here — this HAL talks to the module directly.
 *
 * Wiring (NUCLEO-L073RZ):
 *   E22 RXD <- PA9  (USART1_TX)
 *   E22 TXD -> PA10 (USART1_RX)
 *   E22 M0  <- PC0
 *   E22 M1  <- PC1
 *   E22 AUX -> PC2  (module busy indicator, HIGH = ready)
 *   E22 VCC <- 3.3V, GND <- GND
 *
 * Module modes (M1, M0):    0,0 = transparent TX/RX
 *                           1,0 = configuration (UART fixed 9600 8N1)
 *
 * At boot the HAL enters config mode, reads the registers back (this doubles
 * as a wiring test), writes the working parameters with C2 (volatile — no
 * flash wear), then switches to transparent mode.
 *
 * Radio parameters set here (must be matched by the RAK4631 gateway sniffer):
 *   channel 18 -> 850.125 + 18 = 868.125 MHz, air rate 2.4k, 13 dBm,
 *   transparent mode, sub-packet 240 bytes.
 *
 * If the module does not answer, hal_radio_tx() falls back to printing the
 * hex on the console (same format as hal_sim.c) so the pipeline stays
 * observable while debugging the wiring.
 */

#include "hal.h"
#include "aes_ctr.h"
#include "store_forward.h"
#include "rolling_buffer.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <string.h>

static const uint8_t HW_KEY[AES_KEY_SIZE] = {
    0x2B, 0x7E, 0x15, 0x16,
    0x28, 0xAE, 0xD2, 0xA6,
    0xAB, 0xF7, 0x15, 0x88,
    0x09, 0xCF, 0x4F, 0x3C
};

/* ── devices & pins ───────────────────────────────────────────────────── */

#define EBYTE_UART_NODE DT_NODELABEL(usart1)

#if DT_NODE_HAS_STATUS(EBYTE_UART_NODE, okay)
static const struct device *ebyte_uart = DEVICE_DT_GET(EBYTE_UART_NODE);
#else
static const struct device *ebyte_uart;   /* NULL — overlay missing */
#endif

static const struct device *gpio_c = DEVICE_DT_GET(DT_NODELABEL(gpioc));

#define PIN_M0   0   /* PC0 */
#define PIN_M1   1   /* PC1 */
#define PIN_AUX  2   /* PC2 */

#define EBYTE_AUX_TIMEOUT_MS   1000   /* mode switch / idle wait      */
#define EBYTE_TX_TIMEOUT_MS    5000   /* air TX of one 50-byte frame  */
#define EBYTE_CFG_REPLY_MS      500   /* config command response      */

static bool ebyte_ready;

/* ── low-level helpers ────────────────────────────────────────────────── */

/* AUX is HIGH when the module is idle, LOW while busy (self-check, mode
 * switch, processing, on-air TX). Returns 0 on ready, -1 on timeout. */
static int ebyte_wait_aux(int32_t timeout_ms)
{
    int64_t deadline = k_uptime_get() + timeout_ms;
    while (k_uptime_get() < deadline) {
        if (gpio_pin_get(gpio_c, PIN_AUX) > 0) {
            return 0;
        }
        k_msleep(1);
    }
    return -1;
}

static int ebyte_set_mode(int m1, int m0)
{
    gpio_pin_set(gpio_c, PIN_M0, m0);
    gpio_pin_set(gpio_c, PIN_M1, m1);
    k_msleep(2);
    if (ebyte_wait_aux(EBYTE_AUX_TIMEOUT_MS) != 0) {
        return -1;
    }
    /* datasheet: wait >2 ms after AUX rises before using the new mode */
    k_msleep(10);
    return 0;
}

static void ebyte_uart_flush_rx(void)
{
    unsigned char c;
    while (uart_poll_in(ebyte_uart, &c) == 0) {
        /* discard stale bytes */
    }
}

static void ebyte_uart_send(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(ebyte_uart, buf[i]);
    }
}

static int ebyte_uart_recv(uint8_t *buf, size_t len, int32_t timeout_ms)
{
    size_t  got      = 0;
    int64_t deadline = k_uptime_get() + timeout_ms;
    while (got < len && k_uptime_get() < deadline) {
        unsigned char c;
        if (uart_poll_in(ebyte_uart, &c) == 0) {
            buf[got++] = (uint8_t)c;
        }
        /* no sleep: at 9600 baud a byte lands every ~1 ms and the L0
         * USART has a 1-byte buffer — sleeping a tick causes overruns */
    }
    return (int)got;
}

/* ── module configuration (config mode, 9600 8N1) ─────────────────────── */

/* Registers 00h..06h: ADDH ADDL NETID REG0 REG1 REG2 REG3 */
static const uint8_t EBYTE_PARAMS[7] = {
    0x00,   /* ADDH  — address 0x0000                                  */
    0x00,   /* ADDL                                                    */
    0x00,   /* NETID — network 0                                       */
    0x62,   /* REG0  — UART 9600 8N1, air rate 2.4k                    */
    0x02,   /* REG1  — sub-packet 240 B, RSSI noise off, TX 13 dBm     */
    0x12,   /* REG2  — channel 18 = 850.125 + 18 = 868.125 MHz         */
    0x03,   /* REG3  — transparent mode, RSSI byte off, LBT off        */
};

static int ebyte_configure(void)
{
    uint8_t resp[16];
    int     n;

    if (ebyte_set_mode(1, 0) != 0) {        /* mode 2: configuration */
        printk("[HAL COLLECTOR HW] ERROR: AUX stuck low entering config mode\n");
        return -1;
    }
    ebyte_uart_flush_rx();

    /* 1. Read current registers — doubles as a wiring test.
     * At 9600 baud the 10-byte reply takes ~10 ms; wait 20 ms before reading
     * so the USART buffer has all bytes ready and we don't miss the first one. */
    const uint8_t cmd_read[3] = { 0xC1, 0x00, 0x07 };
    ebyte_uart_send(cmd_read, sizeof(cmd_read));
    n = ebyte_uart_recv(resp, 10, EBYTE_CFG_REPLY_MS);
    printk("[HAL COLLECTOR HW] E22 read reply: got %d bytes:", n);
    for (int i = 0; i < n; i++) printk(" %02X", resp[i]);
    printk("\n");
    if (n < 3 || resp[0] != 0xC1) {
        printk("[HAL COLLECTOR HW] ERROR: E22 not responding (got %d bytes)\n", n);
        printk("[HAL COLLECTOR HW] Check wiring: PA9->RXD, PA10<-TXD, M0=PC0, M1=PC1\n");
        return -1;
    }
    printk("[HAL COLLECTOR HW] E22 factory regs: "
           "addr=%02X%02X net=%02X reg0=%02X reg1=%02X ch=%u reg3=%02X\n",
           resp[3], resp[4], resp[5], resp[6], resp[7], resp[8], resp[9]);

    /* 2. Write working parameters (C2 = volatile, no flash wear) */
    uint8_t cmd_write[3 + sizeof(EBYTE_PARAMS)] = { 0xC2, 0x00, 0x07 };
    memcpy(&cmd_write[3], EBYTE_PARAMS, sizeof(EBYTE_PARAMS));
    ebyte_uart_flush_rx();
    ebyte_uart_send(cmd_write, sizeof(cmd_write));
    n = ebyte_uart_recv(resp, 10, EBYTE_CFG_REPLY_MS);
    if (n < 10 || resp[0] != 0xC1 ||
        memcmp(&resp[3], EBYTE_PARAMS, sizeof(EBYTE_PARAMS)) != 0) {
        printk("[HAL COLLECTOR HW] ERROR: E22 config write failed (got %d bytes)\n", n);
        return -1;
    }
    printk("[HAL COLLECTOR HW] E22 configured: 868.125 MHz, air 2.4k, 13 dBm, transparent\n");

    /* 3. Back to mode 0: transparent transmission */
    if (ebyte_set_mode(0, 0) != 0) {
        printk("[HAL COLLECTOR HW] ERROR: AUX stuck low entering TX mode\n");
        return -1;
    }
    return 0;
}

/* ── init ─────────────────────────────────────────────────────────────── */

int hal_init(void)
{
    printk("[HAL COLLECTOR HW] Initializing (Ebyte E22-900T22S via USART1)\n");

    if (aes_ctr_init(HW_KEY, AES_KEY_SIZE) != 0) {
        printk("[HAL COLLECTOR HW] ERROR: AES init failed\n");
        return SG_HAL_ERROR;
    }

    rolling_buffer_init();
    sf_init();

    if (ebyte_uart == NULL || !device_is_ready(ebyte_uart)) {
        printk("[HAL COLLECTOR HW] USART1 not ready — TX will print hex\n");
        return SG_HAL_OK;
    }
    if (!device_is_ready(gpio_c)) {
        printk("[HAL COLLECTOR HW] GPIOC not ready — TX will print hex\n");
        return SG_HAL_OK;
    }

    gpio_pin_configure(gpio_c, PIN_M0,  GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure(gpio_c, PIN_M1,  GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure(gpio_c, PIN_AUX, GPIO_INPUT);

    /* power-on self check: AUX stays low up to ~100 ms after power-up */
    k_msleep(100);

    if (ebyte_configure() == 0) {
        ebyte_ready = true;
    } else {
        printk("[HAL COLLECTOR HW] E22 unavailable — TX will print hex\n");
    }

    printk("[HAL COLLECTOR HW] Ready\n");
    return SG_HAL_OK;
}

/* ── flash (store-and-forward) ────────────────────────────────────────── */

int hal_flash_write(const uint8_t *packet, size_t len)
{
    if (len != SF_SLOT_SIZE) return SG_HAL_ERROR;
    return (sf_write(packet) == SF_OK) ? SG_HAL_OK : SG_HAL_FULL;
}

int hal_flash_read(uint8_t *packet, size_t len)
{
    if (len != SF_SLOT_SIZE) return SG_HAL_ERROR;
    return (sf_read(packet) == SF_OK) ? SG_HAL_OK : SG_HAL_EMPTY;
}

int hal_flash_pending(void)
{
    return sf_pending();
}

/* ── radio TX ─────────────────────────────────────────────────────────── */

int hal_radio_tx(const uint8_t *packet, size_t len)
{
    if (ebyte_ready) {
        if (ebyte_wait_aux(EBYTE_AUX_TIMEOUT_MS) != 0) {
            printk("[HAL COLLECTOR HW] ERROR: E22 busy, TX skipped\n");
            return SG_HAL_ERROR;
        }

        /* transparent mode: bytes written to the UART go straight on air
         * as one LoRa frame (len < 240-byte sub-packet) */
        ebyte_uart_send(packet, len);

        /* AUX drops while the frame is on air, rises when TX completes */
        k_msleep(10);
        if (ebyte_wait_aux(EBYTE_TX_TIMEOUT_MS) != 0) {
            printk("[HAL COLLECTOR HW] ERROR: E22 TX timeout\n");
            return SG_HAL_ERROR;
        }
        printk("[HAL COLLECTOR HW] E22 LoRa TX %d bytes sent\n", (int)len);
        return SG_HAL_OK;
    }

    /* fallback: same format as hal_sim.c so sim_bridge.py still works */
    printk("[HAL COLLECTOR SIM] LoRa TX %d bytes: ", (int)len);
    for (size_t i = 0; i < len; i++) {
        printk("%02X", packet[i]);
    }
    printk("\n");
    return SG_HAL_OK;
}

/* ── crypto ───────────────────────────────────────────────────────────── */

int hal_crypto_get_key(uint8_t *key_out, size_t key_len)
{
    /*
     * On real hardware: atcab_read_zone(ATCA_ZONE_DATA, KEY_SLOT, ...) via I2C.
     * Returns the hardcoded key until ATECC608B driver is wired.
     */
    if (key_len != AES_KEY_SIZE) return SG_HAL_ERROR;
    memcpy(key_out, HW_KEY, AES_KEY_SIZE);
    return SG_HAL_OK;
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

/* ── sensor ───────────────────────────────────────────────────────────────
 * hal_sensor_read() is NOT implemented here — test_runner generates data
 * directly. Weak stub keeps the linker satisfied if something references it.
 */
__attribute__((weak))
int hal_sensor_read(hal_sensor_data_t *data, uint16_t seq)
{
    (void)data; (void)seq;
    printk("[HAL COLLECTOR HW] hal_sensor_read: no implementation linked\n");
    return SG_HAL_ERROR;
}
