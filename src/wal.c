#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "chainfs.h"
#include "wal.h"
#include "block_mgr.h"

static char wal_path[512];

int wal_init(void) {
    snprintf(wal_path, sizeof(wal_path),
             "%s/wal.log", fs_state->storage_path);
    return wal_recover();
}

int wal_write(uint32_t block_id,
              const uint8_t *data, uint32_t size) {
    FILE *f = fopen(wal_path, "ab");
    if (!f) return -1;
    wal_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.block_id  = block_id;
    entry.size      = size;
    entry.committed = WAL_PENDING;
    memcpy(entry.data, data, size);
    fwrite(&entry, sizeof(wal_entry_t), 1, f);
    fflush(f);
    fclose(f);
    return 0;
}

int wal_commit(uint32_t block_id) {
    FILE *f = fopen(wal_path, "r+b");
    if (!f) return -1;
    wal_entry_t entry;
    while (fread(&entry, sizeof(wal_entry_t), 1, f) == 1) {
        if (entry.block_id == block_id &&
            entry.committed == WAL_PENDING) {
            entry.committed = WAL_COMMITTED;
            fseek(f, -(long)sizeof(wal_entry_t),
                  SEEK_CUR);
            fwrite(&entry, sizeof(wal_entry_t), 1, f);
            break;
        }
    }
    fclose(f);
    return 0;
}

int wal_recover(void) {
    FILE *f = fopen(wal_path, "rb");
    if (!f) return 0;
    wal_entry_t entry;
    int recovered = 0;
    while (fread(&entry, sizeof(wal_entry_t), 1, f) == 1) {
        if (entry.committed == WAL_PENDING) {
            block_write(entry.block_id,
                        entry.data, entry.size);
            recovered++;
            printf("[wal] recovered block_%05u\n",
                   entry.block_id);
        }
    }
    fclose(f);
    if (recovered > 0) {
        printf("[wal] recovered %d operations\n",
               recovered);
        wal_clear();
    }
    return 0;
}

void wal_clear(void) {
    remove(wal_path);
}