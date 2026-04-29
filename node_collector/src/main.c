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
        printk("%02X", buf[i]);
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

        /* 1. Read PLC data via Modbus */
        hal_sensor_data_t data;
        if (hal_sensor_read(&data, seq) != HAL_OK) {
            printk("[COLLECTOR] ERROR: sensor read failed\n");
            continue;
        }

        printk("  State: %d  Pallet: %lu  Wrap: %us  Run: %lus\n",
               data.state, (unsigned long)data.pallet_id,
               data.wrap_time, (unsigned long)data.running_seconds);

        /* 2. Pack into 25-byte payload */
        uint8_t payload[PAYLOAD_SIZE];
        pack_payload(payload,
                     data.state,
                     data.pallet_id,
                     data.wrap_time,
                     data.wrap_transit_time,
                     data.pallet_rotations,
                     data.program_number,
                     data.pallet_perimeter,
                     data.running_seconds,
                     data.alarm_seconds,
                     data.machine_timestamp,
                     seq,
                     DEVICE_ID);

        print_hex("  Payload:    ", payload, PAYLOAD_SIZE);

        /* 3. Build nonce and encrypt */
        uint8_t nonce[AES_NONCE_SIZE];
        hal_crypto_make_nonce(nonce, DEVICE_ID, seq);

        uint8_t ciphertext[PAYLOAD_SIZE];
        aes_ctr_crypt(nonce, payload, ciphertext, PAYLOAD_SIZE);

        /* 4. Build 50-byte rolling redundancy packet */
        uint8_t tx_packet[SF_SLOT_SIZE];
        rolling_buffer_build(ciphertext, tx_packet);

        /* 5. Store in local flash buffer */
        if (hal_flash_write(tx_packet, SF_SLOT_SIZE) == HAL_FULL) {
            printk("  [COLLECTOR] WARNING: buffer full\n");
        }

        /* 6. Transmit via LoRa to gateway */
        printk("  Transmitting via LoRa (%d bytes)...\n", SF_SLOT_SIZE);
        hal_radio_tx(tx_packet, SF_SLOT_SIZE);

        printk("  Buffer pending: %d\n\n", hal_flash_pending());
    }

    printk("[COLLECTOR] Loop complete.\n");
    return 0;
}