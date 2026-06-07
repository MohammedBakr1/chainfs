#ifndef MERKLE_H
#define MERKLE_H

#include "chainfs.h"

void hash_compute(const uint8_t *data,
                  uint32_t size, chainfs_hash_t out);
void hash_block(const uint8_t *data, uint32_t size,
                const chainfs_hash_t prev_hash,
                chainfs_hash_t out);
void merkle_compute(chainfs_inode_t *inode,
                    chainfs_hash_t out);
int  chain_verify(chainfs_inode_t *inode);
void hash_print(const chainfs_hash_t hash);

#endif