#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "chainfs.h"

int storage_init(void) {
    mkdir(fs_state->storage_path, 0755);
    return 0;
}

int state_save(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/state",
             fs_state->storage_path);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite(&fs_state->next_inode_id,
           sizeof(uint32_t), 1, f);
    fwrite(&fs_state->next_block_id,
           sizeof(uint32_t), 1, f);
    fclose(f);
    return 0;
}

int state_load(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/state",
             fs_state->storage_path);
    FILE *f = fopen(path, "rb");
    if (!f) {
        fs_state->next_inode_id = 1;
        fs_state->next_block_id = 1;
        return 0;
    }
    fread(&fs_state->next_inode_id,
          sizeof(uint32_t), 1, f);
    fread(&fs_state->next_block_id,
          sizeof(uint32_t), 1, f);
    fclose(f);
    printf("[chainfs] state loaded — "
           "next_inode=%u next_block=%u\n",
           fs_state->next_inode_id,
           fs_state->next_block_id);
    return 0;
}