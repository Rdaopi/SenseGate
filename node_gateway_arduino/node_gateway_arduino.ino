#include <Adafruit_TinyUSB.h>
#include <bluefruit.h>
#include "hal.h"
#include "role_election.h"
#include "store_forward.h"

#define LED_PIN LED_GREEN

BLEUart bleuart;

static int g_hal_ret = -99;

void ble_println(const char *s)
{
    /* Write unconditionally — bleuart.write() is a no-op when no central is
     * subscribed, and relying on Bluefruit.connected() proved unreliable
     * (it can read false right after the central connects). */
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

    g_hal_ret = hal_init();
    ble_println("[GW] fw=SF9-PRIV-v4");
    role_election_run();

    /* DISTINCTIVE BOOT SIGNATURE: 3 long blinks (700ms on/off).
     * If you still see 6 fast blinks after flashing, the upload did NOT
     * replace the firmware — that is the real problem to fix first. */
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, LOW);
        delay(700);
        digitalWrite(LED_PIN, HIGH);
        delay(700);
    }
}

void loop()
{
    static uint32_t cycle = 0;
    static int boot_logged = 0;
    uint8_t rx_packet[SF_SLOT_SIZE];

    /* Report boot status on the first few cycles so the bridge can confirm
     * the radio initialized regardless of when it subscribed. */
    if (boot_logged < 3) {
        char msg[48];
        snprintf(msg, sizeof(msg), "[GW] boot hal_init=%d", g_hal_ret);
        ble_println(msg);
        delay(100);
        boot_logged++;
    }

    digitalWrite(LED_PIN, LOW);
    delay(50);
    digitalWrite(LED_PIN, HIGH);

    int rx_ret = hal_radio_rx(rx_packet, SF_SLOT_SIZE);

    if (rx_ret != SG_HAL_OK) {
        ble_println("[GW] no LoRa RX (timeout)");
        delay(500);
        return;
    }
    ble_println("[GW] LoRa RX ok");

    if (hal_flash_write(rx_packet, SF_SLOT_SIZE) == SG_HAL_FULL) {
        cycle++;
        delay(2000);
        return;
    }

    node_role_t role = role_election_get();

    if (role == ROLE_MASTER) {
        uint8_t out[SF_SLOT_SIZE];
        while (hal_flash_read(out, SF_SLOT_SIZE) == SG_HAL_OK) {
            if (hal_modem_send_sms(out, SF_SLOT_SIZE) != SG_HAL_OK) {
                /* uplink down — put the packet back and retry next cycle
                 * (the slot we just popped guarantees room) */
                hal_flash_write(out, SF_SLOT_SIZE);
                break;
            }
        }
    }

    cycle++;
    delay(2000);
}
