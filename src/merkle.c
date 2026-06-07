#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>
#include "chainfs.h"
#include "merkle.h"
#include "block_mgr.h"

void hash_compute(const uint8_t *data,
                  uint32_t size, chainfs_hash_t out) {
    SHA256(data, size, out);
}

void hash_block(const uint8_t *data, uint32_t size,
                const chainfs_hash_t prev_hash,
                chainfs_hash_t out) {
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, prev_hash, CHAINFS_HASH_SIZE);
    SHA256_Update(&ctx, data, size);
    SHA256_Final(out, &ctx);
}

void merkle_compute(chainfs_inode_t *inode,
                    chainfs_hash_t out) {
    SHA256_CTX ctx;
    SHA256_Init(&ctx);

    uint8_t block_data[CHAINFS_BLOCK_SIZE];
    chainfs_hash_t prev = {0};

    for (uint32_t i = 0; i < inode->block_count; i++) {
        uint32_t bsize = 0;
        block_read(inode->block_ids[i],
                   block_data, &bsize);

        chainfs_hash_t bhash;
        hash_block(block_data, bsize, prev, bhash);
        SHA256_Update(&ctx, bhash, CHAINFS_HASH_SIZE);
        memcpy(prev, bhash, CHAINFS_HASH_SIZE);
    }

    SHA256_Final(out, &ctx);
}

int chain_verify(chainfs_inode_t *inode) {
    uint8_t        block_data[CHAINFS_BLOCK_SIZE];
    chainfs_hash_t prev = {0};
    chainfs_hash_t computed;

    printf("[verify] file: %s — %u blocks\n",
           inode->name, inode->block_count);

    for (uint32_t i = 0; i < inode->block_count; i++) {
        uint32_t bsize = 0;
        if (block_read(inode->block_ids[i],
                       block_data, &bsize) != 0) {
            printf("[verify] X Block %u: READ ERROR\n", i);
            return -1;
        }
        hash_block(block_data, bsize, prev, computed);
        printf("[verify] Block %u: ", i);
        hash_print(computed);
        printf("\n");
        memcpy(prev, computed, CHAINFS_HASH_SIZE);
    }

    chainfs_hash_t root;
    merkle_compute(inode, root);
    printf("[verify] Merkle root: ");
    hash_print(root);
    printf("\n");

    if (memcmp(root, inode->root_hash,
               CHAINFS_HASH_SIZE) == 0) {
        printf("[verify] CHAIN OK\n");
        return 0;
    }
    printf("[verify] CHAIN BROKEN\n");
    return -1;
}

void hash_print(const chainfs_hash_t hash) {
    for (int i = 0; i < CHAINFS_HASH_SIZE; i++)
        printf("%02x", hash[i]);
}