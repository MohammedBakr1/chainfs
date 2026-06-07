#ifndef BLOCK_MGR_H
#define BLOCK_MGR_H

#include "chainfs.h"

int block_write(uint32_t block_id,
                const uint8_t *data, uint32_t size);
int block_read(uint32_t block_id,
               uint8_t *data, uint32_t *size);
int block_delete(uint32_t block_id);

int inode_write(chainfs_inode_t *inode);
int inode_read(uint32_t inode_id, chainfs_inode_t *inode);
int inode_find_by_name(const char *name,
                       chainfs_inode_t *inode);
int inode_find_by_name_in(const char *name,
                           uint32_t parent_id,
                           chainfs_inode_t *inode);
int inode_find_dir_by_path(const char *path,
                            chainfs_inode_t *inode);
int inode_delete(uint32_t inode_id);

void htable_init(void);
void htable_insert(const char *name,
                   uint32_t parent_id,
                   uint32_t inode_id);
int  htable_lookup(const char *name,
                   uint32_t parent_id,
                   uint32_t *inode_id);
void htable_delete(const char *name,
                   uint32_t parent_id);

#endif