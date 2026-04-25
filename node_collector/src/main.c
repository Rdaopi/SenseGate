#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include "hal.h"
#include "payload_packer.h"
#include "aes_ctr.h"
#include "rolling_buffer.h"
#include "store_forward.h"

#define DEVICE_ID   1
#define NUM_CYCLES  4

static void print_hex(const char *label, const uint8_t *buf, size_t len)
{
    printk("%s", label);
    for (size_t i = 0; i < len; i++) {
        printk("%02X ", buf[i]);
    }
    printk("\n");
}

int main(void)
{
    printk("\nSenseGate -- Data Collector Node\n");
    printk("==================================\n");
    printk("Role: always SENSOR NODE + LoRa TX\n\n");

    if (hal_init() != HAL_OK) {
        printk("FATAL: hal_init failed\n");
        return -1;
    }

    printk("\n[COLLECTOR] Starting loop (%d cycles)\n\n", NUM_CYCLES);

    for (uint16_t seq = 0; seq < NUM_CYCLES; seq++) {

        printk("--- Cycle #%u ---\n", seq);

        /* 1. Leggi sensori e PLC via Modbus */
        hal_sensor_data_t data;
        if (hal_sensor_read(&data, seq) != HAL_OK) {
            printk("[COLLECTOR] ERROR: sensor read failed\n");
            continue;
        }

        /* 2. Pack */
        uint8_t payload[PAYLOAD_SIZE];
        pack_payload(payload,
                     data.temperature,
                     data.humidity,
                     data.vibration,
                     data.pressure,
                     data.plc_cycles,
                     data.plc_hours,
                     data.plc_status,
                     seq,
                     DEVICE_ID);

        print_hex("  Payload:    ", payload, PAYLOAD_SIZE);

        /* 3. Nonce + cifra */
        uint8_t nonce[AES_NONCE_SIZE];
        hal_crypto_make_nonce(nonce, DEVICE_ID, seq);

        uint8_t ciphertext[PAYLOAD_SIZE];
        aes_ctr_crypt(nonce, payload, ciphertext, PAYLOAD_SIZE);

        /* 4. Rolling redundancy → 38 byte */
        uint8_t tx_packet[SF_SLOT_SIZE];
        rolling_buffer_build(ciphertext, tx_packet);

        /* 5. Salva nel buffer locale */
        if (hal_flash_write(tx_packet, SF_SLOT_SIZE) == HAL_FULL) {
            printk("  [COLLECTOR] WARNING: buffer full\n");
        }

        /* 6. Trasmetti via LoRa al gateway */
        printk("  Transmitting via LoRa to gateway...\n");
        hal_radio_tx(tx_packet, SF_SLOT_SIZE);

        printk("  Buffer pending: %d\n\n", hal_flash_pending());
    }

    printk("[COLLECTOR] Loop complete.\n");
    return 0;
}