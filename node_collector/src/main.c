#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include "hal.h"
#include "payload_packer.h"
#include "aes_ctr.h"
#include "rolling_buffer.h"
#include "store_forward.h"
#include "test_runner.h"

#define DEVICE_ID 1


static void on_tx(const hal_sensor_data_t *data,
                  uint16_t seq,
                  const char *scenario_name)
{
    printk("\n=========================================\n");
    printk("  TX #%u  [%s]\n", seq, scenario_name);
    printk("-----------------------------------------\n");
    printk("  INPUT (raw PLC data)\n");
    printk("  pallet_id:    %lu\n",   (unsigned long)data->pallet_id);
    printk("  wrap_time:    %u s\n",  data->wrap_time);
    printk("  wrap_transit: %u s\n",  data->wrap_transit_time);
    printk("  rotations:    %u\n",    data->pallet_rotations);
    printk("  program:      %u\n",    data->program_number);
    printk("  perimeter:    %u\n",    data->pallet_perimeter);
    printk("  running:      %lu s\n", (unsigned long)data->running_seconds);
    printk("  alarm:        %lu s\n", (unsigned long)data->alarm_seconds);
    printk("  timestamp:    %lu\n",   (unsigned long)data->machine_timestamp);
    printk("-----------------------------------------\n");

    /* 1. Pack */
    uint8_t payload[PAYLOAD_SIZE];
    pack_payload(payload,
                 data->state,
                 data->pallet_id,
                 data->wrap_time,
                 data->wrap_transit_time,
                 data->pallet_rotations,
                 data->program_number,
                 data->pallet_perimeter,
                 data->running_seconds,
                 data->alarm_seconds,
                 data->machine_timestamp,
                 seq,
                 DEVICE_ID);

    printk("  PACKED (%d bytes)\n  ", PAYLOAD_SIZE);
    for (int i = 0; i < PAYLOAD_SIZE; i++) printk("%02X", payload[i]);
    printk("\n-----------------------------------------\n");

    /* 2. Encrypt */
    uint8_t nonce[AES_NONCE_SIZE];
    hal_crypto_make_nonce(nonce, DEVICE_ID, seq);
    uint8_t ciphertext[PAYLOAD_SIZE];
    aes_ctr_crypt(nonce, payload, ciphertext, PAYLOAD_SIZE);

    printk("  ENCRYPTED AES-128-CTR (%d bytes)\n  ", PAYLOAD_SIZE);
    for (int i = 0; i < PAYLOAD_SIZE; i++) printk("%02X", ciphertext[i]);
    printk("\n-----------------------------------------\n");

    /* 3. Rolling redundancy */
    uint8_t tx_packet[SF_SLOT_SIZE];
    rolling_buffer_build(ciphertext, tx_packet);

    printk("  TX PACKET current+previous (%d bytes)\n", SF_SLOT_SIZE);
    printk("  [current]  ");
    for (int i = 0; i < PAYLOAD_SIZE; i++) printk("%02X", tx_packet[i]);
    printk("\n  [previous] ");
    for (int i = PAYLOAD_SIZE; i < SF_SLOT_SIZE; i++) printk("%02X", tx_packet[i]);
    printk("\n-----------------------------------------\n");

    /* 4. Store */
    int store_result = hal_flash_write(tx_packet, SF_SLOT_SIZE);
    if (store_result == HAL_FULL) {
        printk("  BUFFER: FULL -- packet dropped\n");
    } else {
        printk("  BUFFER: stored (%d pending)\n", hal_flash_pending());
    }

    /* 5. TX */
    hal_radio_tx(tx_packet, SF_SLOT_SIZE);
    printk("=========================================\n\n");
}

int main(void)
{
    printk("\nSenseGate -- Data Collector Node\n");
    printk("==================================\n\n");

    if (hal_init() != HAL_OK) {
        printk("FATAL: hal_init failed\n");
        return -1;
    }

    test_config_t cfg;
    test_runner_load(&cfg);
    test_runner_run(&cfg, on_tx);

    printk("\nDone. Buffer: %d pending\n", hal_flash_pending());
    return 0;
}