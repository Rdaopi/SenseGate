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

/* Encode `len` bytes as uppercase hex into `dst` (must have room for 2*len+1). */
static void bytes_to_hex(const uint8_t *src, size_t len, char *dst, size_t dst_size)
{
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 2 < dst_size; i++) {
        pos += snprintf(dst + pos, dst_size - pos, "%02X", src[i]);
    }
    dst[pos] = '\0';
}

int hal_modem_send_sms(const uint8_t *payload, size_t len)
{
    char hex[SF_SLOT_SIZE * 2 + 1];
    bytes_to_hex(payload, len, hex, sizeof(hex));

    /* Mirror on USB serial */
    Serial.print("[GATEWAY] NB-IoT TX simulation: ");
    Serial.println(hex);

    /* Send "SMS:<hex>\n" over BLE in 20-byte chunks. We do NOT gate on
     * Bluefruit.connected() — it can read false immediately after a central
     * connects, which silently stalled the whole uplink. bleuart.write()
     * returns 0 when nobody is subscribed, which we handle below. */
    char ble_buf[4 + SF_SLOT_SIZE * 2 + 2];  /* "SMS:" + hex + "\n\0" */
    int  blen = snprintf(ble_buf, sizeof(ble_buf), "SMS:%s\n", hex);
    int  retries = 0;
    for (int sent = 0; sent < blen; ) {
        int chunk = blen - sent;
        if (chunk > 20) chunk = 20;
        int written = (int)bleuart.write((const uint8_t *)ble_buf + sent, chunk);
        if (written <= 0) {
            /* No subscriber yet or TX FIFO congested — back off, then give up
             * so the caller re-queues the packet in store-and-forward. */
            if (++retries > 5) return SG_HAL_ERROR;
            delay(50);
            continue;
        }
        sent += written;
        retries = 0;
        delay(30);
    }

    return SG_HAL_OK;
}

#endif /* CONFIG_NBIOT_REAL */
