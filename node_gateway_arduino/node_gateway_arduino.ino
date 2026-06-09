#include "hal.h"
#include "role_election.h"
#include "store_forward.h"

#define LED_PIN LED_GREEN

void setup()
{
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    hal_init();
    role_election_run();

    /* fast blink = ready */
    for (int i = 0; i < 6; i++) {
        digitalWrite(LED_PIN, LOW);
        delay(100);
        digitalWrite(LED_PIN, HIGH);
        delay(100);
    }
}

void loop()
{
    static uint32_t cycle = 0;
    uint8_t rx_packet[SF_SLOT_SIZE];

    /* heartbeat */
    digitalWrite(LED_PIN, LOW);
    delay(50);
    digitalWrite(LED_PIN, HIGH);

    if (hal_radio_rx(rx_packet, SF_SLOT_SIZE) != SG_HAL_OK) {
        delay(2000);
        return;
    }

    if (hal_flash_write(rx_packet, SF_SLOT_SIZE) == SG_HAL_FULL) {
        cycle++;
        delay(2000);
        return;
    }

    node_role_t role = role_election_get();

    if (role == ROLE_MASTER) {
        uint8_t out[SF_SLOT_SIZE];
        while (hal_flash_read(out, SF_SLOT_SIZE) == SG_HAL_OK) {
            hal_modem_send_sms(out, SF_SLOT_SIZE);
        }
    }

    cycle++;
    delay(2000);
}
