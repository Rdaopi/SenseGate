#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include "hal.h"
#include "role_election.h"
#include "store_forward.h"
#include "rolling_buffer.h"

#define NUM_CYCLES  4

int main(void)
{
    printk("\nSenseGate -- Gateway Node\n");
    printk("==========================\n");
    printk("Role: determined by SIM_DETECT at boot\n\n");

    if (hal_init() != HAL_OK) {
        printk("FATAL: hal_init failed\n");
        return -1;
    }

    printk("\n");

    /* Role election al boot */
    node_role_t role = role_election_run();
    printk("\n");

    printk("[GATEWAY] Starting loop (%d cycles)\n\n", NUM_CYCLES);

    for (int i = 0; i < NUM_CYCLES; i++) {

        printk("--- Gateway cycle #%d ---\n", i);

        /* Ricevi pacchetto da collector via LoRa */
        uint8_t rx_packet[SF_SLOT_SIZE];
        int ret = hal_radio_rx(rx_packet, SF_SLOT_SIZE);
        if (ret != HAL_OK) {
            printk("[GATEWAY] No packet received\n\n");
            continue;
        }

        printk("[GATEWAY] Received 38B via LoRa\n");

        /* Salva nel buffer — non decifra mai */
        if (hal_flash_write(rx_packet, SF_SLOT_SIZE) == HAL_FULL) {
            printk("[GATEWAY] WARNING: buffer full\n");
            continue;
        }

        printk("[GATEWAY] Buffered. Pending: %d\n",
               hal_flash_pending());

        /* Se master: svuota buffer via NB-IoT */
        if (role == ROLE_MASTER) {
            printk("[MASTER] Sending via NB-IoT...\n");
            uint8_t out[SF_SLOT_SIZE];
            int sent = 0;
            while (hal_flash_read(out, SF_SLOT_SIZE) == HAL_OK) {
                hal_modem_send_sms(out, SF_SLOT_SIZE);
                sent++;
            }
            printk("[MASTER] Sent %d packet(s)\n", sent);
        } else {
            printk("[SLAVE] Stored, waiting for master\n");
        }

        printk("\n");
    }

    printk("[GATEWAY] Loop complete.\n");
    return 0;
}