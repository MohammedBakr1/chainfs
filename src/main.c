#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include "chainfs.h"
#include "fuse_ops.h"
#include "storage.h"
#include "wal.h"
#include "block_mgr.h"
#include "crypto.h"
#include "network.h"

chainfs_state_t *fs_state;

static void get_password(char *buf, size_t len) {
    struct termios old, new;
    tcgetattr(0, &old);
    new = old;
    new.c_lflag &= ~ECHO;
    tcsetattr(0, TCSANOW, &new);
    printf("Enter encryption password"
           " (or Enter to skip): ");
    fflush(stdout);
    fgets(buf, len, stdin);
    buf[strcspn(buf, "\n")] = 0;
    tcsetattr(0, TCSANOW, &old);
    printf("\n");
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr,
                "Usage: %s <storage_dir>"
                " <mountpoint>\n", argv[0]);
        return 1;
    }

    fs_state = calloc(1, sizeof(chainfs_state_t));
    if (!fs_state) { perror("calloc"); return 1; }

    strncpy(fs_state->storage_path, argv[1],
            sizeof(fs_state->storage_path) - 1);

    storage_init();
    state_load();
    wal_init();

    if (crypto_load_key(fs_state->storage_path,
                        fs_state->enc_key) == 0) {
        char password[256];
        get_password(password, sizeof(password));

        uint8_t derived_key[32];
        crypto_derive_key(password, derived_key);

        if (memcmp(derived_key,
                   fs_state->enc_key, 32) != 0) {
            fprintf(stderr,
                    "[chainfs] Wrong password!\n");
            return 1;
        }
        fs_state->encrypted = 1;
        printf("[chainfs] Encryption: ON\n");
    } else {
        char password[256];
        get_password(password, sizeof(password));

        if (strlen(password) > 0) {
            crypto_derive_key(password,
                              fs_state->enc_key);
            crypto_save_key(fs_state->storage_path,
                            fs_state->enc_key);
            fs_state->encrypted = 1;
            printf("[chainfs] Encryption: ON\n");
        } else {
            fs_state->encrypted = 0;
            printf("[chainfs] Encryption: OFF\n");
        }
    }

    htable_init();

   
    net_server_start(CHAINFS_PORT);

    char peers_config[512];
    snprintf(peers_config, sizeof(peers_config),
             "%s/peers.conf",
             fs_state->storage_path);
    net_load_peers(peers_config);

    char *fuse_argv[] = {
        argv[0], argv[2], "-f", "-s", NULL
    };
    printf("[chainfs] storage : %s\n",
           fs_state->storage_path);
    printf("[chainfs] mount   : %s\n", argv[2]);

    return fuse_main(4, fuse_argv,
                     &chainfs_ops, NULL);
}