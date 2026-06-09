#include <Adafruit_TinyUSB.h>
#include <bluefruit.h>
#include "hal.h"
#include "role_election.h"
#include "store_forward.h"

#define LED_PIN LED_GREEN

BLEUart bleuart;

static void ble_println(const char *s)
{
    if (!Bluefruit.connected()) return;
    bleuart.write((const uint8_t *)s, strlen(s));
    bleuart.write((const uint8_t *)"\n", 1);
}

void setup()
{
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && (millis() - t0) < 2000) {}

    Bluefruit.begin();
    Bluefruit.setName("SenseGate-GW");
    Bluefruit.setTxPower(4);
    bleuart.begin();
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(bleuart);
    Bluefruit.ScanResponse.addName();
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);
    Bluefruit.Advertising.setFastTimeout(30);
    Bluefruit.Advertising.start(0);

    hal_init();
    role_election_run();

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
