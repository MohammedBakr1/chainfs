#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>

#define AES_KEY_SIZE    32  
#define AES_IV_SIZE     16  


int crypto_encrypt(const uint8_t *key,
                   const uint8_t *data, uint32_t size,
                   uint8_t *out, uint32_t *out_size);


int crypto_decrypt(const uint8_t *key,
                   const uint8_t *data, uint32_t size,
                   uint8_t *out, uint32_t *out_size);


int crypto_derive_key(const char *password,
                      uint8_t *key);

int crypto_save_key(const char *storage_path,
                    const uint8_t *key);

int crypto_load_key(const char *storage_path,
                    uint8_t *key);

#endif