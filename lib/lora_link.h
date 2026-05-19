#ifndef LORA_LINK_H
#define LORA_LINK_H

/*
 * Shared LoRa link parameters for collector (TX) and gateway (RX).
 * Both nodes must use identical settings for the link to work.
 *
 * Target hardware: SX1276 / SX1278 on 868 MHz (Europe)
 * Change LORA_FREQUENCY to 915000000 for US band.
 */

#define LORA_FREQUENCY       868000000  /* Hz */
#define LORA_BANDWIDTH       BW_125_KHZ
#define LORA_DATARATE        SF_10      /* spreading factor */
#define LORA_CODING_RATE     CR_4_5
#define LORA_PREAMBLE_LEN    8
#define LORA_TX_POWER        14         /* dBm, max 14 dBm in EU */
#define LORA_TX_TIMEOUT_MS   3000       /* ms, lora_send() timeout */
#define LORA_RX_TIMEOUT_MS   10000      /* ms, K_MSEC — 0 = block forever */

/*
 * DTS alias expected in board overlay:
 *   / { aliases { lora0 = &lora_dev; }; };
 * Real board overlay example (nRF9160-DK with LoRa shield):
 *   &spi1 { lora_dev: sx1276@0 { ... }; };
 */
#define LORA_NODE DT_ALIAS(lora0)

#endif /* LORA_LINK_H */
