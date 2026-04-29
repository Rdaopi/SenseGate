#include "payload_packer.h"
#include <string.h>

/*
 * CRC16-CCITT (poly 0x1021, init 0xFFFF)
 * Matches Python _crc16_ccitt() in payload_parser.py
 */
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

/*
 * pack_payload — big-endian bit stream, bit 0 = MSB of buf[0].
 * CRC16 computed over bytes 0–22, stored in bytes 23–24.
 * Layout verified to match Python pack() in payload_parser.py.
 */
void pack_payload(uint8_t  *buf,
                  uint8_t   state,
                  uint32_t  pallet_id,
                  uint16_t  wrap_time,
                  uint16_t  wrap_transit_time,
                  uint16_t  pallet_rotations,
                  uint8_t   program_number,
                  uint16_t  pallet_perimeter,
                  uint32_t  running_seconds,
                  uint32_t  alarm_seconds,
                  uint32_t  timestamp,
                  uint16_t  seq_number,
                  uint8_t   device_id)
{
    memset(buf, 0, PAYLOAD_SIZE);

    /* state: bits 0–6 */
    buf[0] |= (state & 0x7F) << 1;

    /* pallet_id: bits 7–24 */
    buf[0] |= (pallet_id >> 17) & 0x01;
    buf[1]  = (pallet_id >> 9)  & 0xFF;
    buf[2]  = (pallet_id >> 1)  & 0xFF;
    buf[3] |= (pallet_id & 0x01) << 7;

    /* wrap_time: bits 25–40 */
    buf[3] |= (wrap_time >> 9)  & 0x7F;
    buf[4]  = (wrap_time >> 1)  & 0xFF;
    buf[5] |= (wrap_time & 0x01) << 7;

    /* wrap_transit_time: bits 41–56 */
    buf[5] |= (wrap_transit_time >> 9) & 0x7F;
    buf[6]  = (wrap_transit_time >> 1) & 0xFF;
    buf[7] |= (wrap_transit_time & 0x01) << 7;

    /* pallet_rotations: bits 57–68 */
    buf[7] |= (pallet_rotations >> 5)  & 0x7F;
    buf[8] |= (pallet_rotations & 0x1F) << 3;

    /* program_number: bits 69–76 */
    buf[8] |= (program_number >> 5) & 0x07;
    buf[9] |= (program_number & 0x1F) << 3;

    /* pallet_perimeter: bits 77–92 */
    buf[9]  |= (pallet_perimeter >> 13) & 0x07;
    buf[10]  = (pallet_perimeter >> 5)  & 0xFF;
    buf[11] |= (pallet_perimeter & 0x1F) << 3;

    /* running_seconds: bits 93–109 */
    buf[11] |= (running_seconds >> 14) & 0x07;
    buf[12]  = (running_seconds >> 6)  & 0xFF;
    buf[13] |= (running_seconds & 0x3F) << 2;

    /* alarm_seconds: bits 110–126 */
    buf[13] |= (alarm_seconds >> 15) & 0x03;
    buf[14]  = (alarm_seconds >> 7)  & 0xFF;
    buf[15] |= (alarm_seconds & 0x7F) << 1;

    /* timestamp: bits 127–158 */
    buf[15] |= (timestamp >> 31) & 0x01;
    buf[16]  = (timestamp >> 23) & 0xFF;
    buf[17]  = (timestamp >> 15) & 0xFF;
    buf[18]  = (timestamp >> 7)  & 0xFF;
    buf[19] |= (timestamp & 0x7F) << 1;

    /* seq_number: bits 159–174 */
    buf[19] |= (seq_number >> 15) & 0x01;
    buf[20]  = (seq_number >> 7)  & 0xFF;
    buf[21] |= (seq_number & 0x7F) << 1;

    /* device_id: bits 175–182 */
    buf[21] |= (device_id >> 7) & 0x01;
    buf[22] |= (device_id & 0x7F) << 1;

    /* bit 183 (byte 22 bit 0) = padding = 0, already zero from memset */

    /* CRC16 over bytes 0–22 */
    uint16_t crc = crc16(buf, PAYLOAD_DATA);
    buf[23] = (crc >> 8) & 0xFF;
    buf[24] = crc & 0xFF;
}

void unpack_payload(const uint8_t *buf,
                    uint8_t  *state,
                    uint32_t *pallet_id,
                    uint16_t *wrap_time,
                    uint16_t *wrap_transit_time,
                    uint16_t *pallet_rotations,
                    uint8_t  *program_number,
                    uint16_t *pallet_perimeter,
                    uint32_t *running_seconds,
                    uint32_t *alarm_seconds,
                    uint32_t *timestamp,
                    uint16_t *seq_number,
                    uint8_t  *device_id)
{
    *state = (buf[0] >> 1) & 0x7F;

    *pallet_id = ((uint32_t)(buf[0] & 0x01) << 17) |
                 ((uint32_t)buf[1]           << 9)  |
                 ((uint32_t)buf[2]           << 1)  |
                 (buf[3] >> 7);

    *wrap_time = ((uint16_t)(buf[3] & 0x7F) << 9) |
                 ((uint16_t)buf[4]           << 1) |
                 (buf[5] >> 7);

    *wrap_transit_time = ((uint16_t)(buf[5] & 0x7F) << 9) |
                         ((uint16_t)buf[6]           << 1) |
                         (buf[7] >> 7);

    *pallet_rotations = ((uint16_t)(buf[7] & 0x7F) << 5) |
                        (buf[8] >> 3);

    *program_number = ((uint8_t)(buf[8] & 0x07) << 5) |
                      (buf[9] >> 3);

    *pallet_perimeter = ((uint16_t)(buf[9]  & 0x07) << 13) |
                        ((uint16_t)buf[10]           << 5)  |
                        (buf[11] >> 3);

    *running_seconds = ((uint32_t)(buf[11] & 0x07) << 14) |
                       ((uint32_t)buf[12]           << 6)  |
                       (buf[13] >> 2);

    *alarm_seconds = ((uint32_t)(buf[13] & 0x03) << 15) |
                     ((uint32_t)buf[14]           << 7)  |
                     (buf[15] >> 1);

    *timestamp = ((uint32_t)(buf[15] & 0x01) << 31) |
                 ((uint32_t)buf[16]           << 23) |
                 ((uint32_t)buf[17]           << 15) |
                 ((uint32_t)buf[18]           << 7)  |
                 (buf[19] >> 1);

    *seq_number = ((uint16_t)(buf[19] & 0x01) << 15) |
                  ((uint16_t)buf[20]           << 7)  |
                  (buf[21] >> 1);

    *device_id = ((uint8_t)(buf[21] & 0x01) << 7) |
                 (buf[22] >> 1);
}