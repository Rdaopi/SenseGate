/*
 * hal_modem.cpp — NB-IoT modem HAL (demo mode)
 * sim_bridge.py is not used in this no-serial build.
 * Real AT command path: uncomment CONFIG_NBIOT_REAL and wire modem to Serial1.
 */

#include "hal.h"
#include "store_forward.h"
#include <Arduino.h>
#include <bluefruit.h>

/* Uncomment when real modem is wired to Serial1 */
// #define CONFIG_NBIOT_REAL

#ifdef CONFIG_NBIOT_REAL

#define MODEM_SERIAL  Serial1
#define MODEM_BAUD    115200
#define MODEM_TIMEOUT 30000

static bool modem_ready = false;

static bool modem_at(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    MODEM_SERIAL.println(cmd);
    uint32_t start = millis();
    String resp = "";
    while (millis() - start < timeout_ms) {
        while (MODEM_SERIAL.available()) resp += (char)MODEM_SERIAL.read();
        if (resp.indexOf(expect) >= 0) return true;
        if (resp.indexOf("ERROR") >= 0) return false;
    }
    return false;
}

void hal_modem_init(void)
{
    MODEM_SERIAL.begin(MODEM_BAUD);
    delay(2000);
    modem_ready = modem_at("AT", "OK", 5000)
               && modem_at("AT+CMGF=0", "OK", 3000);
}

int hal_modem_send_sms(const uint8_t *payload, size_t len)
{
    if (!modem_ready) return SG_HAL_ERROR;
    String hex = "";
    for (size_t i = 0; i < len; i++) {
        if (payload[i] < 0x10) hex += "0";
        hex += String(payload[i], HEX);
    }
    String cmd = "AT+CMGS=\"+39XXXXXXXXXX\"";
    MODEM_SERIAL.println(cmd);
    delay(500);
    MODEM_SERIAL.print(hex);
    MODEM_SERIAL.write(0x1A);
    uint32_t start = millis();
    String resp = "";
    while (millis() - start < MODEM_TIMEOUT) {
        while (MODEM_SERIAL.available()) resp += (char)MODEM_SERIAL.read();
        if (resp.indexOf("OK") >= 0) return SG_HAL_OK;
        if (resp.indexOf("ERROR") >= 0) break;
    }
    return SG_HAL_ERROR;
}

#else /* DEMO MODE — no serial output */

/* forward declaration — defined in .ino */
extern BLEUart bleuart;

void hal_modem_init(void) {}

int hal_modem_send_sms(const uint8_t *payload, size_t len)
{
    /* Build full line: "[GATEWAY] NB-IoT TX simulation: <hex>\n" */
    char line[8 + SF_SLOT_SIZE * 2 + 64];
    int  pos = 0;
    pos += snprintf(line + pos, sizeof(line) - pos, "[GATEWAY] NB-IoT TX simulation: ");
    for (size_t i = 0; i < len && pos + 2 < (int)sizeof(line); i++) {
        pos += snprintf(line + pos, sizeof(line) - pos, "%02X", payload[i]);
    }
    line[pos++] = '\n';
    line[pos]   = '\0';

    /* Send over BLE UART */
    if (Bluefruit.connected())
        bleuart.write((const uint8_t *)line, pos);

    /* Mirror on USB serial */
    Serial.print(line);
    return SG_HAL_OK;
}

#endif /* CONFIG_NBIOT_REAL */
