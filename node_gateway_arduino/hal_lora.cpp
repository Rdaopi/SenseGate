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

static volatile bool rx_done = false;
static volatile bool rx_error = false;
static uint8_t rx_buf[SF_SLOT_SIZE];
static uint8_t rx_len = 0;

static void on_rx_done(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
    if (size >= SF_SLOT_SIZE) {
        memcpy(rx_buf, payload, SF_SLOT_SIZE);
        rx_len = SF_SLOT_SIZE;
        Serial.print("[HAL GW HW] LoRa RX RSSI=");
        Serial.print(rssi);
        Serial.print(" SNR=");
        Serial.println(snr);
    }
    rx_done = true;
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

    Radio.Init(NULL);
    Radio.SetChannel(LORA_FREQUENCY);
    Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SF, LORA_CR,
                      0, LORA_PREAMBLE, 0, false, 0, false, false, 0, false, true);

    RadioEvents_t events;
    memset(&events, 0, sizeof(events));
    events.RxDone    = on_rx_done;
    events.RxError   = on_rx_error;
    events.RxTimeout = on_rx_timeout;
    Radio.Init(&events);

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

    rx_done = false;
    rx_error = false;
    Radio.Rx(LORA_RX_TIMEOUT);

    uint32_t start = millis();
    while (!rx_done && !rx_error && (millis() - start < LORA_RX_TIMEOUT + 500)) {
        Radio.IrqProcess();
        delay(1);
    }

    if (rx_done) {
        memcpy(packet, rx_buf, SF_SLOT_SIZE);
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
