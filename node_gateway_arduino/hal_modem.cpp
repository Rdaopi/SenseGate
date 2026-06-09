/*
 * hal_modem.cpp — NB-IoT modem HAL
 *
 * DEMO MODE (current): prints hex payload on Serial.
 * sim_bridge.py captures the "[GATEWAY] NB-IoT TX" line and
 * forwards it to the Docker stack (Twilio sim → AWS IoT → Grafana).
 *
 * REAL HW (future): uncomment CONFIG_NBIOT_REAL and wire a
 * Quectel BC660K-GL or similar to Serial1 (UART0 on RAK19007).
 */

#include "hal.h"
#include <Arduino.h>

/* Uncomment when real modem is wired to Serial1 */
// #define CONFIG_NBIOT_REAL

#ifdef CONFIG_NBIOT_REAL

#define MODEM_SERIAL  Serial1
#define MODEM_BAUD    115200
#define MODEM_TIMEOUT 30000   /* ms */

static bool modem_ready = false;

static bool modem_at(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    MODEM_SERIAL.println(cmd);
    uint32_t start = millis();
    String resp = "";
    while (millis() - start < timeout_ms) {
        while (MODEM_SERIAL.available()) {
            resp += (char)MODEM_SERIAL.read();
        }
        if (resp.indexOf(expect) >= 0) return true;
        if (resp.indexOf("ERROR") >= 0) return false;
    }
    return false;
}

void hal_modem_init(void)
{
    MODEM_SERIAL.begin(MODEM_BAUD);
    delay(2000);
    modem_ready  = modem_at("AT", "OK", 5000)
                && modem_at("AT+CMGF=0", "OK", 3000); /* PDU mode */
    Serial.println(modem_ready ? "[MODEM] Ready" : "[MODEM] Init failed");
}

int hal_modem_send_sms(const uint8_t *payload, size_t len)
{
    if (!modem_ready) return SG_HAL_ERROR;

    /* Build hex string */
    String hex = "";
    for (size_t i = 0; i < len; i++) {
        if (payload[i] < 0x10) hex += "0";
        hex += String(payload[i], HEX);
    }

    /* AT+CMGS=<length in bytes> */
    String cmd = "AT+CMGS=\"+39XXXXXXXXXX\""; /* TODO: set MSISDN */
    MODEM_SERIAL.println(cmd);
    delay(500);
    MODEM_SERIAL.print(hex);
    MODEM_SERIAL.write(0x1A); /* CTRL+Z — triggers send */

    uint32_t start = millis();
    String resp = "";
    while (millis() - start < MODEM_TIMEOUT) {
        while (MODEM_SERIAL.available()) resp += (char)MODEM_SERIAL.read();
        if (resp.indexOf("OK") >= 0) {
            Serial.println("[MODEM] SMS sent OK");
            return SG_HAL_OK;
        }
        if (resp.indexOf("ERROR") >= 0) break;
    }
    Serial.println("[MODEM] SMS send failed");
    return SG_HAL_ERROR;
}

#else /* DEMO MODE — output captured by sim_bridge.py */

void hal_modem_init(void)
{
    Serial.println("[MODEM SIM] Demo mode — output via Serial");
}

int hal_modem_send_sms(const uint8_t *payload, size_t len)
{
    Serial.print("[GATEWAY] NB-IoT TX simulation: ");
    for (size_t i = 0; i < len; i++) {
        if (payload[i] < 0x10) Serial.print("0");
        Serial.print(payload[i], HEX);
    }
    Serial.println();
    return SG_HAL_OK;
}

#endif /* CONFIG_NBIOT_REAL */
