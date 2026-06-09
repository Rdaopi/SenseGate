/*
 * SenseGate — node_gateway (Arduino / RAK4631)
 * Console output via Serial1 (UART, pins TX=P0.16 RX=P0.15 on RAK19007)
 * Connect a USB-UART adapter to see output, OR use Serial (USB) if your
 * core version supports it.
 */

#include <Adafruit_TinyUSB.h>  // From RAK core built-in library
#include "hal.h"
#include "role_election.h"
#include "store_forward.h"

#define DBG Serial

void setup()
{
    DBG.begin(115200);
    delay(1000);

    DBG.println("\n*** SenseGate Gateway Node ***");
    DBG.println("==============================");

    if (hal_init() != SG_HAL_OK) {
        DBG.println("FATAL: hal_init failed");
        while (1);
    }

    node_role_t role = role_election_run();
    DBG.print("Role: ");
    DBG.println(role_to_string(role));
    DBG.println();
    DBG.println("[GATEWAY] Starting -- waiting for LoRa packets");
}

void loop()
{
    static uint32_t cycle = 0;
    uint8_t rx_packet[SF_SLOT_SIZE];

    if (hal_radio_rx(rx_packet, SF_SLOT_SIZE) != SG_HAL_OK) {
        delay(2000);
        return;
    }

    DBG.print("[GATEWAY] RX packet seq=");
    DBG.print(cycle);
    DBG.print(" (");
    DBG.print(SF_SLOT_SIZE);
    DBG.println(" bytes via LoRa)");

    if (hal_flash_write(rx_packet, SF_SLOT_SIZE) == SG_HAL_FULL) {
        DBG.println("[GATEWAY] WARNING: buffer full, dropping");
        cycle++;
        delay(2000);
        return;
    }

    DBG.print("[GATEWAY] Buffered (");
    DBG.print(hal_flash_pending());
    DBG.println(" pending)");

    node_role_t role = role_election_get();

    if (role == ROLE_MASTER) {
        uint8_t out[SF_SLOT_SIZE];
        int sent = 0;
        while (hal_flash_read(out, SF_SLOT_SIZE) == SG_HAL_OK) {
            if (hal_modem_send_sms(out, SF_SLOT_SIZE) == SG_HAL_OK) {
                sent++;
            }
        }
        DBG.print("[MASTER] Sent ");
        DBG.print(sent);
        DBG.print("  pending ");
        DBG.println(hal_flash_pending());
    } else {
        DBG.println("[SLAVE] Stored, relaying toward master");
    }

    cycle++;
    delay(2000);
}
