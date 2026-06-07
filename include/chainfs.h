#ifndef CHAINFS_H
#define CHAINFS_H

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdint.h>
#include <time.h>

#define CHAINFS_BLOCK_SIZE  4096
#define CHAINFS_HASH_SIZE   32
#define CHAINFS_MAX_NAME    255
#define CHAINFS_MAX_BLOCKS  262144
#define CHAINFS_MAX_PATH    512

#define INODE_FILE    0
#define INODE_DIR     1
#define INODE_SYMLINK 2

typedef uint8_t chainfs_hash_t[CHAINFS_HASH_SIZE];

typedef struct {
    uint32_t        inode_id;
    uint32_t        parent_id;
    uint8_t         type;
    uint32_t        nlink;
    uint32_t        size;
    uint32_t        block_count;
    uint32_t        block_ids[CHAINFS_MAX_BLOCKS];
    chainfs_hash_t  root_hash;
    struct timespec atime, mtime, ctime;
    char            name[CHAINFS_MAX_NAME];
    char            symlink_target[CHAINFS_MAX_PATH];
} chainfs_inode_t;

typedef struct {
    char     storage_path[512];
    uint32_t next_inode_id;
    uint32_t next_block_id;
    uint8_t  enc_key[32];
    uint8_t  encrypted;
} chainfs_state_t;

extern chainfs_state_t *fs_state;

#endif