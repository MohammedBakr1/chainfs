#ifndef WAL_H
#define WAL_H

#include "chainfs.h"

#define WAL_PENDING   0
#define WAL_COMMITTED 1

typedef struct {
    uint32_t block_id;
    uint32_t size;
    uint8_t  data[CHAINFS_BLOCK_SIZE];
    uint8_t  committed;
} wal_entry_t;

int  wal_init(void);
int  wal_write(uint32_t block_id,
               const uint8_t *data, uint32_t size);
int  wal_commit(uint32_t block_id);
int  wal_recover(void);
void wal_clear(void);

#endif