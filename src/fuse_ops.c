#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include "chainfs.h"
#include "block_mgr.h"
#include "merkle.h"
#include "storage.h"
#include "wal.h"

static int path_split(const char *path,
                      uint32_t *parent_id,
                      char *name) {
    char tmp[512];
    strncpy(tmp, path, sizeof(tmp) - 1);
    char *slash = strrchr(tmp, '/');
    if (!slash) return -EINVAL;
    strncpy(name, slash + 1, CHAINFS_MAX_NAME - 1);
    if (slash == tmp) {
        *parent_id = 0;
        return 0;
    }
    *slash = '\0';
    chainfs_inode_t parent;
    if (inode_find_dir_by_path(tmp, &parent) != 0)
        return -ENOENT;
    *parent_id = parent.inode_id;
    return 0;
}

static int chainfs_getattr(const char *path,
                           struct stat *st) {
    memset(st, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0) {
        st->st_mode  = S_IFDIR | 0755;
        st->st_nlink = 2;
        st->st_uid   = getuid();
        st->st_gid   = getgid();
        return 0;
    }
    uint32_t parent_id;
    char name[CHAINFS_MAX_NAME];
    if (path_split(path, &parent_id, name) != 0)
        return -ENOENT;
    chainfs_inode_t inode;
    if (inode_find_by_name_in(name,
                               parent_id, &inode) != 0)
        return -ENOENT;

    switch (inode.type) {
        case INODE_DIR:
            st->st_mode  = S_IFDIR | 0755;
            st->st_nlink = 2;
            break;
        case INODE_SYMLINK:
            st->st_mode  = S_IFLNK | 0777;
            st->st_nlink = 1;
            st->st_size  = strlen(inode.symlink_target);
            break;
        default: /* INODE_FILE */
            st->st_mode  = S_IFREG | 0644;
            st->st_nlink = inode.nlink;
            st->st_size  = inode.size;
            break;
    }
    st->st_uid   = getuid();
    st->st_gid   = getgid();
    st->st_atime = inode.atime.tv_sec;
    st->st_mtime = inode.mtime.tv_sec;
    st->st_ctime = inode.ctime.tv_sec;
    return 0;
}

static int chainfs_readdir(const char *path, void *buf,
                           fuse_fill_dir_t filler,
                           off_t offset,
                           struct fuse_file_info *fi) {
    (void) offset; (void) fi;
    filler(buf, ".",  NULL, 0);
    filler(buf, "..", NULL, 0);
    uint32_t parent_id = 0;
    if (strcmp(path, "/") != 0) {
        chainfs_inode_t dir;
        if (inode_find_dir_by_path(path, &dir) != 0)
            return -ENOENT;
        parent_id = dir.inode_id;
    }
    for (uint32_t id = 1;
         id < fs_state->next_inode_id; id++) {
        chainfs_inode_t inode;
        if (inode_read(id, &inode) == 0 &&
            inode.parent_id == parent_id)
            filler(buf, inode.name, NULL, 0);
    }
    return 0;
}

static int chainfs_mkdir(const char *path,
                         mode_t mode) {
    (void) mode;
    uint32_t parent_id;
    char name[CHAINFS_MAX_NAME];
    if (path_split(path, &parent_id, name) != 0)
        return -ENOENT;
    chainfs_inode_t existing;
    if (inode_find_by_name_in(name,
                               parent_id,
                               &existing) == 0)
        return -EEXIST;
    chainfs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.inode_id  = fs_state->next_inode_id++;
    inode.parent_id = parent_id;
    inode.type      = INODE_DIR;
    inode.nlink     = 2;
    strncpy(inode.name, name, CHAINFS_MAX_NAME - 1);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    inode.atime = inode.mtime = inode.ctime = ts;
    inode_write(&inode);
    htable_insert(inode.name,
                  inode.parent_id,
                  inode.inode_id);
    state_save();
    return 0;
}static int chainfs_rmdir(const char *path) {
    uint32_t parent_id;
    char name[CHAINFS_MAX_NAME];
    if (path_split(path, &parent_id, name) != 0)
        return -ENOENT;
    chainfs_inode_t inode;
    if (inode_find_by_name_in(name,
                               parent_id, &inode) != 0)
        return -ENOENT;
    if (inode.type != INODE_DIR) return -ENOTDIR;
    htable_delete(inode.name, inode.parent_id);
    inode_delete(inode.inode_id);
    state_save();
    return 0;
}

static int chainfs_create(const char *path,
                          mode_t mode,
                          struct fuse_file_info *fi) {
    (void) mode; (void) fi;
    uint32_t parent_id;
    char name[CHAINFS_MAX_NAME];
    if (path_split(path, &parent_id, name) != 0)
        return -ENOENT;
    chainfs_inode_t existing;
    if (inode_find_by_name_in(name,
                               parent_id,
                               &existing) == 0)
        return -EEXIST;
    chainfs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.inode_id  = fs_state->next_inode_id++;
    inode.parent_id = parent_id;
    inode.type      = INODE_FILE;
    inode.nlink     = 1;
    inode.size      = 0;
    strncpy(inode.name, name, CHAINFS_MAX_NAME - 1);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    inode.atime = inode.mtime = inode.ctime = ts;
    inode_write(&inode);
    htable_insert(inode.name,
                  inode.parent_id,
                  inode.inode_id);
    state_save();
    return 0;
}

static int chainfs_open(const char *path,
                        struct fuse_file_info *fi) {
    (void) fi;
    uint32_t parent_id;
    char name[CHAINFS_MAX_NAME];
    if (path_split(path, &parent_id, name) != 0)
        return -ENOENT;
    chainfs_inode_t inode;
    return inode_find_by_name_in(name,
                                  parent_id, &inode);
}

static int chainfs_read(const char *path, char *buf,
                        size_t size, off_t offset,
                        struct fuse_file_info *fi) {
    (void) fi;
    uint32_t parent_id;
    char name[CHAINFS_MAX_NAME];
    if (path_split(path, &parent_id, name) != 0)
        return -ENOENT;
    chainfs_inode_t inode;
    if (inode_find_by_name_in(name,
                               parent_id, &inode) != 0)
        return -ENOENT;
    size_t total = 0;
    uint8_t block_data[CHAINFS_BLOCK_SIZE];
    for (uint32_t i = 0;
         i < inode.block_count && total < size; i++) {
        uint32_t bsize = 0;
        if (block_read(inode.block_ids[i],
                       block_data, &bsize) != 0) break;
        off_t block_start = i * CHAINFS_BLOCK_SIZE;
        off_t block_end   = block_start + bsize;
        if (offset >= block_end) continue;
        off_t  start    = (offset > block_start) ?
                          offset - block_start : 0;
        size_t can_read = bsize - start;
        if (can_read > size - total)
            can_read = size - total;
        memcpy(buf + total, block_data + start,
               can_read);
        total += can_read;
    }
    return (int)total;
}

static int chainfs_write(const char *path,
                         const char *buf,
                         size_t size, off_t offset,
                         struct fuse_file_info *fi) {
    (void) fi; (void) offset;
    uint32_t parent_id;
    char name[CHAINFS_MAX_NAME];
    if (path_split(path, &parent_id, name) != 0)
        return -ENOENT;
    chainfs_inode_t inode;
    if (inode_find_by_name_in(name,
                               parent_id, &inode) != 0)
        return -ENOENT;
    size_t written = 0;
    while (written < size) {
        uint32_t block_id = fs_state->next_block_id++;
        size_t chunk = size - written;
        if (chunk > CHAINFS_BLOCK_SIZE)
            chunk = CHAINFS_BLOCK_SIZE;
        const uint8_t *data =
            (const uint8_t*)buf + written;
        wal_write(block_id, data, chunk);
        if (block_write(block_id, data, chunk) != 0)
            return -EIO;
        wal_commit(block_id);
        inode.block_ids[inode.block_count++] = block_id;
        written += chunk;
    }
    inode.size += size;
    merkle_compute(&inode, inode.root_hash);
    printf("[chainfs] write %s — root_hash: ",
           inode.name);
    hash_print(inode.root_hash);
    printf("\n");
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    inode.mtime = ts;
    inode_write(&inode);
    state_save();
    wal_clear();
    return (int)size;
}


static int chainfs_symlink(const char *target,
                           const char *path) {
    uint32_t parent_id;
    char name[CHAINFS_MAX_NAME];
    if (path_split(path, &parent_id, name) != 0)
        return -ENOENT;

    chainfs_inode_t existing;
    if (inode_find_by_name_in(name,
                               parent_id,
                               &existing) == 0)
        return -EEXIST;

    chainfs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.inode_id  = fs_state->next_inode_id++;
    inode.parent_id = parent_id;
    inode.type      = INODE_SYMLINK;
    inode.nlink     = 1;
    strncpy(inode.name, name, CHAINFS_MAX_NAME - 1);
    strncpy(inode.symlink_target, target,
            CHAINFS_MAX_PATH - 1);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    inode.atime = inode.mtime = inode.ctime = ts;

    inode_write(&inode);
    htable_insert(inode.name,
                  inode.parent_id,
                  inode.inode_id);
    state_save();
    return 0;
}

static int chainfs_readlink(const char *path,
                            char *buf, size_t size) {
    uint32_t parent_id;
    char name[CHAINFS_MAX_NAME];
    if (path_split(path, &parent_id, name) != 0)
        return -ENOENT;

    chainfs_inode_t inode;
    if (inode_find_by_name_in(name,
                               parent_id, &inode) != 0)
        return -ENOENT;

    if (inode.type != INODE_SYMLINK)
        return -EINVAL;

    strncpy(buf, inode.symlink_target, size - 1);
    buf[size - 1] = '\0';
    return 0;
}


static int chainfs_link(const char *oldpath,
                        const char *newpath) {
    
    uint32_t old_parent;
    char old_name[CHAINFS_MAX_NAME];
    if (path_split(oldpath, &old_parent,
                   old_name) != 0)
        return -ENOENT;

    chainfs_inode_t inode;
    if (inode_find_by_name_in(old_name,
                               old_parent, &inode) != 0)
        return -ENOENT;

    if (inode.type == INODE_DIR)
        return -EPERM; 

    
    uint32_t new_parent;
    char new_name[CHAINFS_MAX_NAME];
    if (path_split(newpath, &new_parent,
                   new_name) != 0)
        return -ENOENT;

    chainfs_inode_t existing;
    if (inode_find_by_name_in(new_name,
                               new_parent,
                               &existing) == 0)
        return -EEXIST;

   
    chainfs_inode_t link_inode = inode;
    link_inode.inode_id  = fs_state->next_inode_id++;
    link_inode.parent_id = new_parent;
    strncpy(link_inode.name, new_name,
            CHAINFS_MAX_NAME - 1);

    
    inode.nlink++;
    inode_write(&inode);

   
    inode_write(&link_inode);
    htable_insert(link_inode.name,
                  link_inode.parent_id,
                  link_inode.inode_id);
    state_save();
    return 0;
}

static int chainfs_rename(const char *oldpath,
                          const char *newpath) {uint32_t old_parent;
    char old_name[CHAINFS_MAX_NAME];
    if (path_split(oldpath, &old_parent,
                   old_name) != 0)
        return -ENOENT;
    uint32_t new_parent;
    char new_name[CHAINFS_MAX_NAME];
    if (path_split(newpath, &new_parent,
                   new_name) != 0)
        return -ENOENT;
    chainfs_inode_t inode;
    if (inode_find_by_name_in(old_name,
                               old_parent, &inode) != 0)
        return -ENOENT;
    htable_delete(inode.name, inode.parent_id);
    strncpy(inode.name, new_name,
            CHAINFS_MAX_NAME - 1);
    inode.parent_id = new_parent;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    inode.ctime = ts;
    inode_write(&inode);
    htable_insert(inode.name,
                  inode.parent_id,
                  inode.inode_id);
    state_save();
    return 0;
}

static int chainfs_unlink(const char *path) {
    uint32_t parent_id;
    char name[CHAINFS_MAX_NAME];
    if (path_split(path, &parent_id, name) != 0)
        return -ENOENT;
    chainfs_inode_t inode;
    if (inode_find_by_name_in(name,
                               parent_id, &inode) != 0)
        return -ENOENT;

    
    if (inode.nlink > 1) {
        inode.nlink--;
        inode_write(&inode);
        htable_delete(inode.name, inode.parent_id);
        inode_delete(inode.inode_id);
        state_save();
        return 0;
    }

   
    for (uint32_t i = 0; i < inode.block_count; i++)
        block_delete(inode.block_ids[i]);
    htable_delete(inode.name, inode.parent_id);
    inode_delete(inode.inode_id);
    state_save();
    return 0;
}

static int chainfs_truncate(const char *path,
                            off_t size) {
    (void) size;
    uint32_t parent_id;
    char name[CHAINFS_MAX_NAME];
    if (path_split(path, &parent_id, name) != 0)
        return -ENOENT;
    chainfs_inode_t inode;
    if (inode_find_by_name_in(name,
                               parent_id, &inode) != 0)
        return -ENOENT;
    for (uint32_t i = 0; i < inode.block_count; i++)
        block_delete(inode.block_ids[i]);
    inode.block_count = 0;
    inode.size        = 0;
    memset(inode.root_hash, 0, CHAINFS_HASH_SIZE);
    inode_write(&inode);
    state_save();
    return 0;
}

static int chainfs_utimens(const char *path,
                           const struct timespec tv[2]) {
    if (strcmp(path, "/") == 0) return 0;
    uint32_t parent_id;
    char name[CHAINFS_MAX_NAME];
    if (path_split(path, &parent_id, name) != 0)
        return -ENOENT;
    chainfs_inode_t inode;
    if (inode_find_by_name_in(name,
                               parent_id, &inode) != 0)
        return -ENOENT;
    inode.atime = tv[0];
    inode.mtime = tv[1];
    inode_write(&inode);
    return 0;
}

static int chainfs_statfs(const char *path,
                          struct statvfs *st) {
    (void) path;
    statvfs(fs_state->storage_path, st);
    return 0;
}

static int chainfs_chmod(const char *path,
                         mode_t mode) {
    (void) path; (void) mode;
    return 0;
}

static int chainfs_chown(const char *path,
                         uid_t uid, gid_t gid) {
    (void) path; (void) uid; (void) gid;
    return 0;
}

struct fuse_operations chainfs_ops = {
    .getattr  = chainfs_getattr,
    .readdir  = chainfs_readdir,
    .mkdir    = chainfs_mkdir,
    .rmdir    = chainfs_rmdir,
    .create   = chainfs_create,
    .open     = chainfs_open,
    .read     = chainfs_read,
    .write    = chainfs_write,
    .rename   = chainfs_rename,
    .unlink   = chainfs_unlink,
    .truncate = chainfs_truncate,
    .utimens  = chainfs_utimens,
    .statfs   = chainfs_statfs,
    .chmod    = chainfs_chmod,
    .chown    = chainfs_chown,
    .symlink  = chainfs_symlink,   
    .readlink = chainfs_readlink,  
    .link     = chainfs_link,      
};