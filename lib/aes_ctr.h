#ifndef AES_CTR_H
#define AES_CTR_H

#include <stdint.h>
#include <stddef.h>

/* AES-128-CTR — key 16 byte, nonce 16 byte
 * CTR mode: same function for encrypt and decrypt
 * 19B in → 19B out, no padding needed
 */

#define AES_KEY_SIZE   16
#define AES_NONCE_SIZE 16

int  aes_ctr_init(const uint8_t *key, size_t key_len);
void aes_ctr_crypt(const uint8_t *nonce,
                   const uint8_t *input,
                   uint8_t       *output,
                   size_t         len);

#endif