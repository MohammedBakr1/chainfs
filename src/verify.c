#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chainfs.h"
#include "block_mgr.h"
#include "merkle.h"

chainfs_state_t *fs_state;

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr,
                "Usage: %s <storage_dir> <filename>\n",
                argv[0]);
        return 1;
    }

    fs_state = calloc(1, sizeof(chainfs_state_t));
    strncpy(fs_state->storage_path, argv[1],
            sizeof(fs_state->storage_path) - 1);
    fs_state->next_inode_id = 1;
    fs_state->next_block_id = 1;

    const char *filename = argv[2];
    chainfs_inode_t inode;
    int found = 0;

    for (uint32_t id = 1; id < 10000; id++) {
        if (inode_read(id, &inode) == 0) {
            fs_state->next_inode_id = id + 1;
            if (strcmp(inode.name, filename) == 0) {
                found = 1;
                break;
            }
        }
    }

    if (!found) {
        fprintf(stderr,
                "[verify] ERROR: '%s' not found\n",
                filename);
        return 1;
    }

    printf("[verify] ══════════════════════════\n");
    printf("[verify] File   : %s\n", inode.name);
    printf("[verify] Size   : %u bytes\n", inode.size);
    printf("[verify] Blocks : %u\n", inode.block_count);
    printf("[verify] Stored root:\n[verify]   ");
    hash_print(inode.root_hash);
    printf("\n");
    printf("[verify] ══════════════════════════\n");

    uint8_t        block_data[CHAINFS_BLOCK_SIZE];
    chainfs_hash_t prev = {0};
    chainfs_hash_t computed;
    int ok = 1;

    for (uint32_t i = 0; i < inode.block_count; i++) {
        uint32_t bsize = 0;
        if (block_read(inode.block_ids[i],
                       block_data, &bsize) != 0) {
            printf("[verify] X Block %u: READ ERROR\n",
                   i);
            ok = 0;
            continue;
        }
        hash_block(block_data, bsize, prev, computed);
        printf("[verify] Block %u: ", i);
        hash_print(computed);
        printf(" OK\n");
        memcpy(prev, computed, CHAINFS_HASH_SIZE);
    }

    chainfs_hash_t current_root;
    merkle_compute(&inode, current_root);

    printf("[verify] ══════════════════════════\n");
    printf("[verify] Computed root:\n[verify]   ");
    hash_print(current_root);
    printf("\n");

    if (memcmp(current_root, inode.root_hash,
               CHAINFS_HASH_SIZE) == 0 && ok)
        printf("[verify] CHAIN INTACT\n");
    else
        printf("[verify] CHAIN BROKEN - TAMPERED!\n");

    printf("[verify] ══════════════════════════\n");
    free(fs_state);
    return 0;
}