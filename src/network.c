#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "chainfs.h"
#include "network.h"
#include "block_mgr.h"

net_state_t *net_state;

static int send_all(int sock, const void *buf,
                    size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(sock,
                         (const char*)buf + sent,
                         len - sent, 0);
        if (n <= 0) return -1;
        sent += n;
    }
    return 0;
}

static int recv_all(int sock, void *buf,
                    size_t len) {
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(sock,
                         (char*)buf + got,
                         len - got, 0);
        if (n <= 0) return -1;
        got += n;
    }
    return 0;
}

static void handle_client(int client_sock) {
    net_msg_header_t hdr;

    while (recv_all(client_sock, &hdr,
                    sizeof(hdr)) == 0) {
        switch (hdr.type) {

        case MSG_WRITE_BLOCK: {
            uint8_t *data = malloc(hdr.size);
            if (!data) break;
            recv_all(client_sock, data, hdr.size);
            block_write(hdr.block_id, data, hdr.size);
            free(data);

            net_msg_header_t ack = {0};
            ack.type     = MSG_ACK;
            ack.block_id = hdr.block_id;
            send_all(client_sock, &ack, sizeof(ack));

            printf("[net] stored block_%05u\n",
                   hdr.block_id);
            break;
        }

        case MSG_READ_BLOCK: {
            uint8_t data[CHAINFS_BLOCK_SIZE];
            uint32_t size = 0;
            net_msg_header_t resp = {0};

            if (block_read(hdr.block_id,
                           data, &size) == 0) {
                resp.type     = MSG_BLOCK_DATA;
                resp.block_id = hdr.block_id;
                resp.size     = size;
                send_all(client_sock, &resp,
                         sizeof(resp));
                send_all(client_sock, data, size);
            } else {
                resp.type = MSG_ERROR;
                send_all(client_sock, &resp,
                         sizeof(resp));
            }
            break;
        }

        case MSG_HEARTBEAT: {
            net_msg_header_t ack = {0};
            ack.type = MSG_ACK;
            send_all(client_sock, &ack, sizeof(ack));
            break;
        }

        default:
            break;
        }
    }

    close(client_sock);
}

static void *server_thread(void *arg) {
    (void) arg;

    while (net_state->running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_sock = accept(
            net_state->server_sock,
            (struct sockaddr*)&client_addr,
            &addr_len);

        if (client_sock < 0) continue;

        printf("[net] connection from %s\n",
               inet_ntoa(client_addr.sin_addr));

        handle_client(client_sock);
    }

    return NULL;
}

int net_server_start(uint16_t port) {
    net_state = calloc(1, sizeof(net_state_t));
    if (!net_state) return -1;

    net_state->server_sock = socket(AF_INET,
                                    SOCK_STREAM, 0);
    if (net_state->server_sock < 0) return -1;

    int opt = 1;
    setsockopt(net_state->server_sock, SOL_SOCKET,
               SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(net_state->server_sock,
             (struct sockaddr*)&addr,
             sizeof(addr)) < 0) return -1;

    if (listen(net_state->server_sock, 10) < 0)
        return -1;

    net_state->running = 1;

    pthread_t tid;
    pthread_create(&tid, NULL, server_thread, NULL);
    pthread_detach(tid);

    printf("[net] server listening on port %u\n",
           port);
    return 0;
}void net_server_stop(void) {
    if (net_state) {
        net_state->running = 0;
        close(net_state->server_sock);
    }
}

int net_peer_connect(const char *ip,
                     uint16_t port) {
    if (!net_state) return -1;
    if (net_state->peer_count >= CHAINFS_MAX_PEERS)
        return -1;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&addr,
                sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    int idx = net_state->peer_count++;
    strncpy(net_state->peers[idx].ip, ip, 63);
    net_state->peers[idx].port   = port;
    net_state->peers[idx].sock   = sock;
    net_state->peers[idx].active = 1;

    printf("[net] connected to peer %s:%u\n",
           ip, port);
    return idx;
}

void net_peer_disconnect(int idx) {
    if (!net_state) return;
    if (idx < 0 || idx >= net_state->peer_count)
        return;
    close(net_state->peers[idx].sock);
    net_state->peers[idx].active = 0;
}

int net_block_replicate(uint32_t block_id,
                        const uint8_t *data,
                        uint32_t size) {
    if (!net_state) return 0;

    int success = 0;

    for (int i = 0;
         i < net_state->peer_count; i++) {
        if (!net_state->peers[i].active) continue;

        net_msg_header_t hdr = {0};
        hdr.type     = MSG_WRITE_BLOCK;
        hdr.block_id = block_id;
        hdr.size     = size;

        int sock = net_state->peers[i].sock;

        if (send_all(sock, &hdr,
                     sizeof(hdr)) != 0) continue;
        if (send_all(sock, data, size) != 0)
            continue;

        net_msg_header_t ack;
        if (recv_all(sock, &ack,
                     sizeof(ack)) == 0 &&
            ack.type == MSG_ACK) {
            success++;
            printf("[net] block_%05u → %s\n",
                   block_id,
                   net_state->peers[i].ip);
        }
    }

    return success;
}

int net_block_fetch(const char *ip, uint16_t port,
                    uint32_t block_id,
                    uint8_t *data, uint32_t *size) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&addr,
                sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    net_msg_header_t hdr = {0};
    hdr.type     = MSG_READ_BLOCK;
    hdr.block_id = block_id;
    send_all(sock, &hdr, sizeof(hdr));

    net_msg_header_t resp;
    recv_all(sock, &resp, sizeof(resp));

    if (resp.type == MSG_BLOCK_DATA) {
        *size = resp.size;
        recv_all(sock, data, resp.size);
        close(sock);
        return 0;
    }

    close(sock);
    return -1;
}

void net_load_peers(const char *config_file) {
    FILE *f = fopen(config_file, "r");
    if (!f) return;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        if (line[0] == '#' || line[0] == '\0')
            continue;
        char ip[64];
        uint16_t port;
        if (sscanf(line, "%63s %hu",
                   ip, &port) == 2)
            net_peer_connect(ip, port);
    }

    fclose(f);
}