#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include "chainfs.h"
#include "block_mgr.h"
#include "crypto.h"


#define HTABLE_SIZE 4096

typedef struct htable_entry {
    char     name[CHAINFS_MAX_NAME];
    uint32_t parent_id;
    uint32_t inode_id;
    struct htable_entry *next;
} htable_entry_t;

static htable_entry_t *htable[HTABLE_SIZE];

static uint32_t htable_hash(const char *name,
                             uint32_t parent_id) {
    uint32_t h = parent_id * 31;
    while (*name) h = h * 31 + (uint8_t)*name++;
    return h % HTABLE_SIZE;
}

void htable_init(void) {
    memset(htable, 0, sizeof(htable));
    for (uint32_t id = 1;
         id < fs_state->next_inode_id; id++) {
        chainfs_inode_t inode;
        if (inode_read(id, &inode) == 0)
            htable_insert(inode.name,
                          inode.parent_id,
                          inode.inode_id);
    }
}

void htable_insert(const char *name,
                   uint32_t parent_id,
                   uint32_t inode_id) {
    uint32_t h = htable_hash(name, parent_id);
    htable_entry_t *e = calloc(1, sizeof(*e));
    strncpy(e->name, name, CHAINFS_MAX_NAME - 1);
    e->parent_id = parent_id;
    e->inode_id  = inode_id;
    e->next      = htable[h];
    htable[h]    = e;
}

int htable_lookup(const char *name,
                  uint32_t parent_id,
                  uint32_t *inode_id) {
    uint32_t h = htable_hash(name, parent_id);
    htable_entry_t *e = htable[h];
    while (e) {
        if (e->parent_id == parent_id &&
            strcmp(e->name, name) == 0) {
            *inode_id = e->inode_id;
            return 0;
        }
        e = e->next;
    }
    return -1;
}

void htable_delete(const char *name,
                   uint32_t parent_id) {
    uint32_t h = htable_hash(name, parent_id);
    htable_entry_t **pp = &htable[h];
    while (*pp) {
        if ((*pp)->parent_id == parent_id &&
            strcmp((*pp)->name, name) == 0) {
            htable_entry_t *tmp = *pp;
            *pp = tmp->next;
            free(tmp);
            return;
        }
        pp = &(*pp)->next;
    }
}

static void block_path(uint32_t block_id,
                       char *path, size_t len) {
    snprintf(path, len, "%s/block_%05u",
             fs_state->storage_path, block_id);
}

static void inode_path(uint32_t inode_id,
                       char *path, size_t len) {
    snprintf(path, len, "%s/inode_%05u",
             fs_state->storage_path, inode_id);
}

int block_write(uint32_t block_id,
                const uint8_t *data, uint32_t size) {
    char path[512];
    block_path(block_id, path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (!f) return -EIO;

    if (fs_state->encrypted) {
        uint8_t encrypted[CHAINFS_BLOCK_SIZE + 64];
        uint32_t enc_size = 0;
        crypto_encrypt(fs_state->enc_key,
                        data, size,
                        encrypted, &enc_size);
        fwrite(&enc_size, sizeof(uint32_t), 1, f);
        fwrite(encrypted, 1, enc_size, f);
    } else {
        fwrite(&size, sizeof(uint32_t), 1, f);
        fwrite(data, 1, size, f);
    }

    fclose(f);
    return 0;
}

int block_read(uint32_t block_id,
               uint8_t *data, uint32_t *size) {
    char path[512];
    block_path(block_id, path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) return -ENOENT;

    uint32_t stored_size;
    fread(&stored_size, sizeof(uint32_t), 1, f);

    if (fs_state->encrypted) {
        uint8_t encrypted[CHAINFS_BLOCK_SIZE + 64];
        fread(encrypted, 1, stored_size, f);
        fclose(f);
        crypto_decrypt(fs_state->enc_key,
                        encrypted, stored_size,
                        data, size);
    } else {
        fread(data, 1, stored_size, f);
        *size = stored_size;
        fclose(f);
    }

    return 0;
}int block_delete(uint32_t block_id) {
    char path[512];
    block_path(block_id, path, sizeof(path));
    return remove(path) == 0 ? 0 : -EIO;
}


int inode_write(chainfs_inode_t *inode) {
    char path[512];
    inode_path(inode->inode_id, path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (!f) return -EIO;
    fwrite(inode, sizeof(chainfs_inode_t), 1, f);
    fclose(f);
    return 0;
}

int inode_read(uint32_t inode_id,
               chainfs_inode_t *inode) {
    char path[512];
    inode_path(inode_id, path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) return -ENOENT;
    fread(inode, sizeof(chainfs_inode_t), 1, f);
    fclose(f);
    return 0;
}

int inode_find_by_name(const char *name,
                       chainfs_inode_t *inode) {
    return inode_find_by_name_in(name, 0, inode);
}

int inode_find_by_name_in(const char *name,
                           uint32_t parent_id,
                           chainfs_inode_t *inode) {
    uint32_t inode_id;
    if (htable_lookup(name, parent_id, &inode_id) != 0)
        return -ENOENT;
    return inode_read(inode_id, inode);
}

int inode_find_dir_by_path(const char *path,
                            chainfs_inode_t *inode) {
    if (strcmp(path, "/") == 0) {
        memset(inode, 0, sizeof(*inode));
        inode->inode_id = 0;
        inode->type     = INODE_DIR;
        return 0;
    }

    char tmp_path[512];
    strncpy(tmp_path, path, sizeof(tmp_path) - 1);

    uint32_t parent_id = 0;
    char *token = strtok(tmp_path + 1, "/");
    chainfs_inode_t current;
    memset(&current, 0, sizeof(current));

    while (token) {
        if (inode_find_by_name_in(token,
                                   parent_id,
                                   &current) != 0)
            return -ENOENT;
        parent_id = current.inode_id;
        token = strtok(NULL, "/");
    }

    *inode = current;
    return 0;
}

int inode_delete(uint32_t inode_id) {
    char path[512];
    inode_path(inode_id, path, sizeof(path));
    return remove(path) == 0 ? 0 : -EIO;
}