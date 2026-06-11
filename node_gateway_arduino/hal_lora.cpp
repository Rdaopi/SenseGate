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

/* LoRa link parameters — must match node_collector */
#define LORA_FREQUENCY   868000000
#define LORA_BANDWIDTH   0          /* 125 kHz */
#define LORA_SF          10
#define LORA_CR          1          /* 4/5 */
#define LORA_PREAMBLE    8
#define LORA_TX_DBM      14
#define LORA_RX_TIMEOUT  10000      /* ms */

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
    if (size >= SF_SLOT_SIZE && rx_count < RX_RING_SLOTS) {
        memcpy(rx_ring[rx_tail], payload, SF_SLOT_SIZE);
        rx_tail = (rx_tail + 1) % RX_RING_SLOTS;
        noInterrupts();
        rx_count++;
        interrupts();
        Serial.print("[HAL GW HW] LoRa RX RSSI=");
        Serial.print(rssi);
        Serial.print(" SNR=");
        Serial.println(snr);
    }
}

static void on_rx_error(void)
{
    rx_error = true;
}

static void on_rx_timeout(void)
{
    rx_error = true;
}

int hal_init(void)
{
    Serial.println("[HAL GW HW] Initializing SX1262...");
    sf_init();

    lora_hardware_init(_hwConfig);

    RadioEvents_t events;
    memset(&events, 0, sizeof(events));
    events.RxDone    = on_rx_done;
    events.RxError   = on_rx_error;
    events.RxTimeout = on_rx_timeout;
    Radio.Init(&events);

    Radio.SetChannel(LORA_FREQUENCY);
    Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SF, LORA_CR,
                      0, LORA_PREAMBLE, 0, false, 0, false, false, 0, false, true);

    lora_ready = true;
    Serial.println("[HAL GW HW] SX1262 ready");
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

    /* Frame already buffered from a previous RX window? Consume it directly. */
    if (rx_count == 0) {
        rx_error = false;
        Radio.Rx(LORA_RX_TIMEOUT);

        /* RxDone is delivered by the library's background task on nRF52 —
         * no IrqProcess() pump needed, just wait for the ring to fill. */
        uint32_t start = millis();
        while (rx_count == 0 && !rx_error && (millis() - start < LORA_RX_TIMEOUT + 500)) {
            delay(1);
        }
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
