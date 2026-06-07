#include <stdio.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include "crypto.h"
#include "chainfs.h"

int crypto_encrypt(const uint8_t *key,
                   const uint8_t *data, uint32_t size,
                   uint8_t *out, uint32_t *out_size) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;

    uint8_t iv[AES_IV_SIZE];
    RAND_bytes(iv, AES_IV_SIZE);

   
    memcpy(out, iv, AES_IV_SIZE);

    int len = 0, total = 0;

    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(),
                       NULL, key, iv);
    EVP_EncryptUpdate(ctx, out + AES_IV_SIZE,
                      &len, data, size);
    total += len;
    EVP_EncryptFinal_ex(ctx, out + AES_IV_SIZE + total,
                        &len);
    total += len;

    *out_size = AES_IV_SIZE + total;
    EVP_CIPHER_CTX_free(ctx);
    return 0;
}


int crypto_decrypt(const uint8_t *key,
                   const uint8_t *data, uint32_t size,
                   uint8_t *out, uint32_t *out_size) {
    if (size < AES_IV_SIZE) return -1;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;

    
    const uint8_t *iv   = data;
    const uint8_t *ciphertext = data + AES_IV_SIZE;
    uint32_t ciphertext_len   = size - AES_IV_SIZE;

    int len = 0, total = 0;

    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(),
                       NULL, key, iv);
    EVP_DecryptUpdate(ctx, out, &len,
                      ciphertext, ciphertext_len);
    total += len;
    EVP_DecryptFinal_ex(ctx, out + total, &len);
    total += len;

    *out_size = total;
    EVP_CIPHER_CTX_free(ctx);
    return 0;
}


int crypto_derive_key(const char *password,
                      uint8_t *key) {
    SHA256((const uint8_t*)password,
           strlen(password), key);
    return 0;
}


int crypto_save_key(const char *storage_path,
                    const uint8_t *key) {
    char path[512];
    snprintf(path, sizeof(path),
             "%s/.key", storage_path);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite(key, 1, AES_KEY_SIZE, f);
    fclose(f);
    return 0;
}


int crypto_load_key(const char *storage_path,
                    uint8_t *key) {
    char path[512];
    snprintf(path, sizeof(path),
             "%s/.key", storage_path);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fread(key, 1, AES_KEY_SIZE, f);
    fclose(f);
    return 0;
}