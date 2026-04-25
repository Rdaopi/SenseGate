#include "hal.h"
#include "store_forward.h"
#include "rolling_buffer.h"
#include <string.h>
#include <zephyr/sys/printk.h>

static hal_role_t sim_role = HAL_ROLE_SLAVE;
static uint16_t   sim_seq  = 0;

/* Pacchetto finto — simula quello che manda il collector */
static const uint8_t SIM_PACKET[SF_SLOT_SIZE] = {
    0x5B,0xDE,0xB6,0x99,0x9B,0x33,0x97,0x1B,
    0x85,0xA4,0x59,0x2D,0xE3,0x35,0xCA,0x3E,
    0x44,0x75,0x8B,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00
};

int hal_init(void)
{
    printk("[HAL GW SIM] Initializing gateway\n");
    sf_init();
    rolling_buffer_init();
    printk("[HAL GW SIM] Ready. Role: %s\n",
           sim_role == HAL_ROLE_MASTER ? "MASTER" : "SLAVE");
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

int hal_radio_rx(uint8_t *packet, size_t len)
{
    /*
     * Simula ricezione LoRa dal collector.
     * Adesso: copia un pacchetto finto
     * Dopo:   driver Zephyr LoRa RX:
     *         lora_recv(&lora_dev, packet, len, K_FOREVER, &rssi, &snr)
     */
    if (len < SF_SLOT_SIZE) return HAL_ERROR;
    memcpy(packet, SIM_PACKET, SF_SLOT_SIZE);
    /* Varia leggermente per simulare pacchetti diversi */
    packet[0] ^= (uint8_t)sim_seq;
    sim_seq++;
    printk("[HAL GW SIM] LoRa RX simulated (seq %u)\n", sim_seq);
    return HAL_OK;
}

int hal_modem_send_sms(const uint8_t *payload, size_t len)
{
    /*
     * Simula invio SMS NB-IoT.
     * Dopo: AT+CMGF=0 + AT+CMGS + payload hex + CTRL+Z
     */
    printk("[HAL GW SIM] NB-IoT SMS (%d bytes): ", len);
    for (size_t i = 0; i < len; i++) {
        printk("%02X", payload[i]);
    }
    printk("\n");
    return HAL_OK;
}

hal_role_t hal_get_role(void)
{
    return sim_role;
}

void hal_sim_set_role(hal_role_t role)
{
    sim_role = role;
}