#ifndef PAYLOAD_PACKER_H
#define PAYLOAD_PACKER_H

#include <stdint.h>
#include <stddef.h>

/* Fixed payload dimension byte */
#define PAYLOAD_SIZE 19

/* Calculate the CRC16-CCITT */
uint16_t crc16(const uint8_t *data, size_t len);

/* Packeting the payload data */
void pack_payload(uint8_t *buf,
                  float temperature,
                  float humidity,
                  float vibration,
                  float pressure,
                  uint32_t plc_cycles,
                  uint16_t plc_hours,
                  uint8_t plc_status,
                  uint16_t seq_number,
                  uint8_t device_id);

/* Unpacling data from the buffer */
void unpack_payload(const uint8_t *buf,
                    float *temperature,
                    float *humidity,
                    float *vibration,
                    float *pressure,
                    uint32_t *plc_cycles,
                    uint16_t *plc_hours,
                    uint8_t *plc_status,
                    uint16_t *seq_number,
                    uint8_t *device_id);

#endif