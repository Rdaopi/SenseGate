#include "payload_packer.h"
#include <string.h>

/* CRC16-CCITT */
uint16_t crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
        }
    }
    return crc;
}

void pack_payload(uint8_t *buf,
                  float temperature,
                  float humidity,
                  float vibration,
                  float pressure,
                  uint32_t plc_cycles,
                  uint16_t plc_hours,
                  uint8_t plc_status,
                  uint16_t seq_number,
                  uint8_t device_id)
{
    memset(buf, 0, PAYLOAD_SIZE);

    /* Temperature: offset +40, scale x10, 12 bit */
    uint16_t temp_raw = (uint16_t)((temperature + 40.0f) * 10.0f);
    buf[0] = (temp_raw >> 4) & 0xFF;
    buf[1] = (temp_raw & 0x0F) << 4;

    /* Humidity: scale x2, 8 bit */
    uint8_t hum_raw = (uint8_t)(humidity * 2.0f);
    buf[1] |= (hum_raw >> 4) & 0x0F;
    buf[2]  = (hum_raw & 0x0F) << 4;

    /* Vibration: scale x64, 10 bit */
    uint16_t vib_raw = (uint16_t)(vibration * 64.0f);
    buf[2] |= (vib_raw >> 6) & 0x0F;
    buf[3]  = (vib_raw & 0x3F) << 2;

    /* Pressure: offset -300, scale x16, 14 bit */
    uint16_t pres_raw = (uint16_t)((pressure - 300.0f) * 16.0f);
    buf[3] |= (pres_raw >> 12) & 0x03;
    buf[4]  = (pres_raw >> 4) & 0xFF;
    buf[5]  = (pres_raw & 0x0F) << 4;

    /* PLC cycles: 24 bit */
    buf[6]  = (plc_cycles >> 16) & 0xFF;
    buf[7]  = (plc_cycles >> 8)  & 0xFF;
    buf[8]  = (plc_cycles)       & 0xFF;

    /* PLC hours: 16 bit */
    buf[9]  = (plc_hours >> 8) & 0xFF;
    buf[10] = (plc_hours)      & 0xFF;

    /* PLC status: 8 bit */
    buf[11] = plc_status;

    /* Sequence number: 16 bit */
    buf[12] = (seq_number >> 8) & 0xFF;
    buf[13] = (seq_number)      & 0xFF;

    /* Device ID: 8 bit */
    buf[14] = device_id;

    /* Padding bytes 15-16 — reserved */
    buf[15] = 0x00;
    buf[16] = 0x00;

    /* CRC16 on the first 17 byte */
    uint16_t crc = crc16(buf, 17);
    buf[17] = (crc >> 8) & 0xFF;
    buf[18] = crc & 0xFF;
}

void unpack_payload(const uint8_t *buf,
                    float *temperature,
                    float *humidity,
                    float *vibration,
                    float *pressure,
                    uint32_t *plc_cycles,
                    uint16_t *plc_hours,
                    uint8_t *plc_status,
                    uint16_t *seq_number,
                    uint8_t *device_id)
{
    uint16_t temp_raw  = ((uint16_t)buf[0] << 4) | (buf[1] >> 4);
    *temperature = (temp_raw / 10.0f) - 40.0f;

    uint8_t hum_raw    = ((buf[1] & 0x0F) << 4) | (buf[2] >> 4);
    *humidity    = hum_raw / 2.0f;

    uint16_t vib_raw   = ((uint16_t)(buf[2] & 0x0F) << 6) | (buf[3] >> 2);
    *vibration   = vib_raw / 64.0f;

    uint16_t pres_raw  = ((uint16_t)(buf[3] & 0x03) << 12) |
                         ((uint16_t)buf[4] << 4) |
                         (buf[5] >> 4);
    *pressure    = (pres_raw / 16.0f) + 300.0f;

    *plc_cycles  = ((uint32_t)buf[6] << 16) |
                   ((uint32_t)buf[7] << 8)  |
                   buf[8];

    *plc_hours   = ((uint16_t)buf[9] << 8) | buf[10];
    *plc_status  = buf[11];
    *seq_number  = ((uint16_t)buf[12] << 8) | buf[13];
    *device_id   = buf[14];
}