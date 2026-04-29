#ifndef PAYLOAD_PACKER_H
#define PAYLOAD_PACKER_H

#include <stdint.h>
#include <stddef.h>

/*
 * Bit-packing layout (25 bytes total = 23 data + 2 CRC):
 *
 *  Bit   0–6   : state             (7  bits) — machine state code
 *  Bit   7–24  : pallet_id         (18 bits) — current pallet ID
 *  Bit  25–40  : wrap_time         (16 bits) — wrapping time in seconds
 *  Bit  41–56  : wrap_transit_time (16 bits) — wrapping + transit time in seconds
 *  Bit  57–68  : pallet_rotations  (12 bits) — rotations on pallet
 *  Bit  69–76  : program_number    (8  bits) — program number
 *  Bit  77–92  : pallet_perimeter  (16 bits) — pallet perimeter (unit TBD)
 *  Bit  93–109 : running_seconds   (17 bits) — daily accumulated running time
 *  Bit 110–126 : alarm_seconds     (17 bits) — daily accumulated alarm time
 *  Bit 127–158 : timestamp         (32 bits) — Unix epoch from machine clock
 *  Bit 159–174 : sequence          (16 bits) — packet counter
 *  Bit 175–182 : device_id         (8  bits) — node identifier
 *  Bit 183     : padding           (1  bit)  — zero
 *  Bytes 0–22  : data (184 bits = 23 bytes)  — CRC computed over these
 *  Bytes 23–24 : CRC16-CCITT big-endian
 *
 * TOTAL: 25 bytes
 * Rolling redundancy (current + previous): 50 bytes → 100 hex chars via SMS
 */

#define PAYLOAD_SIZE   25   /* total bytes including CRC  */
#define PAYLOAD_DATA   23   /* bytes over which CRC is computed */

/* Machine state codes */
#define STATE_POWER_ON          0
#define STATE_ONLINE            19
#define STATE_STOP              20
#define STATE_RUNNING           30
#define STATE_WAITING_MATERIALS 31
#define STATE_WAITING_UPSTREAM  32
#define STATE_OFFLINE           99

/* CRC16-CCITT (poly 0x1021, init 0xFFFF) */
uint16_t crc16(const uint8_t *data, size_t len);

/* Pack TWIKO fields into a 25-byte payload */
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
                  uint8_t   device_id);

/* Unpack a 25-byte payload back into fields (does NOT verify CRC) */
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
                    uint8_t  *device_id);

#endif /* PAYLOAD_PACKER_H */