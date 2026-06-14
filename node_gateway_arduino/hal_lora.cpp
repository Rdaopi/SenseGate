/*
 * hal_lora.cpp — real hardware HAL for RAK4631 (SX1262 via SX126x-Arduino)
 * Active when HAL_USE_LORA is defined (see hal_select.h)
 */
#include "hal_select.h"
#ifdef HAL_USE_LORA

#include "hal.h"
#include "store_forward.h"
#include <Arduino.h>
#include <SX126x-Arduino.h>

/* forward declaration — defined in node_gateway_arduino.ino */
extern void ble_println(const char *s);

/* LoRa link parameters — E22-900T22S at REG0=0x62 (air rate 2.4k = SF9 BW125)
 * REG2=0x12 → channel 18 = 850.125 + 18 = 868.125 MHz
 * Syncword: E22 uses private syncword 0x12 → SX1262 register value 0x1424
 *           SetPublicNetwork(false) sets 0x1424, SetPublicNetwork(true) sets 0x3444 */
#define LORA_FREQUENCY   868125000
#define LORA_BANDWIDTH   0          /* 125 kHz */
#define LORA_SF          9          /* E22 air rate 2.4k = SF9 */
#define LORA_CR          1          /* 4/5 — E22 fixes CR internally */
#define LORA_PREAMBLE    8          /* standard LoRa default */
#define LORA_TX_DBM      14
#define LORA_RX_TIMEOUT  8000       /* ms */

static bool lora_ready = false;
static hal_role_t hw_role = HAL_ROLE_MASTER;

/* RX ring buffer: on nRF52 the SX126x-Arduino library delivers RxDone from a
 * background task, so frames can arrive while loop() is busy (e.g. draining
 * the modem). A one-deep buffer would drop them; the ring keeps them until
 * hal_radio_rx is called. Single producer (radio task) / single consumer
 * (loop) — only rx_count is shared, guarded by interrupt locks. */
#define RX_RING_SLOTS 4
static uint8_t rx_ring[RX_RING_SLOTS][SF_SLOT_SIZE];
static volatile int  rx_head  = 0;
static volatile int  rx_tail  = 0;
static volatile int  rx_count = 0;
static volatile bool rx_error = false;

static void on_rx_done(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "[GW] RX SF9 size=%u RSSI=%d SNR=%d", size, rssi, snr);
    Serial.println(buf);
    ble_println(buf);

    if (size > 0 && rx_count < RX_RING_SLOTS) {
        uint16_t copy = size < SF_SLOT_SIZE ? size : SF_SLOT_SIZE;
        memcpy(rx_ring[rx_tail], payload, copy);
        rx_tail = (rx_tail + 1) % RX_RING_SLOTS;
        noInterrupts();
        rx_count++;
        interrupts();
    }
}

static void on_rx_error(void)
{
    rx_error = true;
    Serial.println("[GW] RX error");
}

static void on_rx_timeout(void)
{
    rx_error = true;
    Serial.println("[GW] RX timeout");
}

int hal_init(void)
{
    Serial.println("[HAL GW HW] Initializing SX1262...");
    sf_init();

    /* RAK4631: use the board-specific init that configures the WisBlock Core
     * SX1262 pin mapping automatically. Using lora_hardware_init() with an
     * uninitialized _hwConfig hangs forever waiting on the BUSY line. */
    uint32_t rc = lora_rak4630_init();
    if (rc != 0) {
        Serial.print("[HAL GW HW] lora_rak4630_init failed rc=");
        Serial.println(rc);
        return SG_HAL_ERROR;
    }

    RadioEvents_t events;
    memset(&events, 0, sizeof(events));
    events.RxDone    = on_rx_done;
    events.RxError   = on_rx_error;
    events.RxTimeout = on_rx_timeout;
    Radio.Init(&events);

    Radio.SetPublicNetwork(false);  /* E22 private syncword 0x12 → 0x1424 */

    Radio.SetChannel(LORA_FREQUENCY);
    Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SF, LORA_CR,
                      0, LORA_PREAMBLE, 0, false, 0, false, false, 0, false, true);

    lora_ready = true;
    Serial.println("[HAL GW HW] SX1262 ready v2");
    return SG_HAL_OK;
}

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

int hal_radio_rx(uint8_t *packet, size_t len)
{
    if (!lora_ready || len < SF_SLOT_SIZE) return SG_HAL_ERROR;

    if (rx_count == 0) {
        Radio.Sleep();

        /* E22 private syncword = 0x12 → SX1262 stores as 0x1424.
         * SetPublicNetwork(false) writes 0x1424 to regs 0x0740/0x0741. */
        Radio.SetPublicNetwork(false);

        Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SF, LORA_CR,
                          0, LORA_PREAMBLE, 0, false, 0, false,
                          false, 0, false, true);

        rx_error = false;
        Radio.Rx(0);

        /* Read back syncword registers to confirm what's actually set */
        uint8_t sw0 = SX126xReadRegister(0x0740);
        uint8_t sw1 = SX126xReadRegister(0x0741);
        char swmsg[48];
        snprintf(swmsg, sizeof(swmsg), "[GW] SW=%02X%02X SF9 868MHz", sw0, sw1);
        Serial.println(swmsg);
        ble_println(swmsg);

        uint32_t start = millis();
        while (rx_count == 0 && !rx_error && (millis() - start < LORA_RX_TIMEOUT)) {
            delay(10);
        }
        Radio.Sleep();
    }

    if (rx_count > 0) {
        memcpy(packet, rx_ring[rx_head], SF_SLOT_SIZE);
        rx_head = (rx_head + 1) % RX_RING_SLOTS;
        noInterrupts();
        rx_count--;
        interrupts();
        return SG_HAL_OK;
    }
    return SG_HAL_ERROR;
}

hal_role_t hal_get_role(void)
{
    return hw_role;
}

void hal_sim_set_role(hal_role_t role)
{
    hw_role = role;
}

#endif /* HAL_USE_LORA */
