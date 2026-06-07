#ifndef NETWORK_H
#define NETWORK_H

#include <stdint.h>
#include "chainfs.h"

#define CHAINFS_PORT      8080
#define CHAINFS_MAX_PEERS 16
#define CHAINFS_BUF_SIZE  (CHAINFS_BLOCK_SIZE + 256)

#define MSG_WRITE_BLOCK   0x01
#define MSG_READ_BLOCK    0x02
#define MSG_BLOCK_DATA    0x03
#define MSG_VERIFY        0x04
#define MSG_HEARTBEAT     0x05
#define MSG_ACK           0x06
#define MSG_ERROR         0x07
#define MSG_SYNC_REQUEST  0x08
#define MSG_SYNC_DATA     0x09

typedef struct {
    uint8_t  type;
    uint32_t size;
    uint32_t block_id;
    uint8_t  checksum[32];
} __attribute__((packed)) net_msg_header_t;

typedef struct {
    char     ip[64];
    uint16_t port;
    int      sock;
    uint8_t  active;
} peer_t;

typedef struct {
    int      server_sock;
    peer_t   peers[CHAINFS_MAX_PEERS];
    uint8_t  peer_count;
    uint8_t  running;
} net_state_t;

extern net_state_t *net_state;

int  net_server_start(uint16_t port);
void net_server_stop(void);
int  net_peer_connect(const char *ip, uint16_t port);
void net_peer_disconnect(int peer_idx);
int  net_block_replicate(uint32_t block_id,
                         const uint8_t *data,
                         uint32_t size);
int  net_block_fetch(const char *ip, uint16_t port,
                     uint32_t block_id,
                     uint8_t *data, uint32_t *size);
void net_load_peers(const char *config_file);

#endif