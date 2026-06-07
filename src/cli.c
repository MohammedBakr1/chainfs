#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define STORE_PATH "/var/chainfs/store"
#define MOUNT_PATH "/mnt/chainfs"

static void usage(void) {
    printf("Usage: chainfs <command>\n\n");
    printf("Commands:\n");
    printf("  mount          Mount filesystem\n");
    printf("  umount         Unmount filesystem\n");
    printf("  status         Show status\n");
    printf("  verify <file>  Verify file integrity\n");
    printf("  ls             List files\n");
}

static void cmd_mount(void) {
    mkdir(STORE_PATH, 0755);
    mkdir(MOUNT_PATH, 0755);
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "/usr/bin/chainfs-daemon %s %s &",
             STORE_PATH, MOUNT_PATH);
    system(cmd);
    printf("[chainfs] mounted at %s\n", MOUNT_PATH);
}

static void cmd_umount(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "fusermount -u %s", MOUNT_PATH);
    system(cmd);
    printf("[chainfs] unmounted\n");
}

static void cmd_status(void) {
    struct stat st;
    if (stat(MOUNT_PATH, &st) != 0) {
        printf("[chainfs] not mounted\n");
        return;
    }
    printf("[chainfs] mounted at %s\n", MOUNT_PATH);
    printf("[chainfs] storage  : %s\n", STORE_PATH);
}

static void cmd_ls(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ls -lh %s", MOUNT_PATH);
    system(cmd);
}

static void cmd_verify(const char *filename) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "/usr/bin/chainfs-verify %s %s",
             STORE_PATH, filename);
    system(cmd);
}

int main(int argc, char *argv[]) {
    if (argc < 2) { usage(); return 1; }
    if      (!strcmp(argv[1], "mount"))  cmd_mount();
    else if (!strcmp(argv[1], "umount")) cmd_umount();
    else if (!strcmp(argv[1], "status")) cmd_status();
    else if (!strcmp(argv[1], "ls"))     cmd_ls();
    else if (!strcmp(argv[1], "verify")) {
        if (argc < 3) {
            fprintf(stderr,
                    "Usage: chainfs verify <file>\n");
            return 1;
        }
        cmd_verify(argv[2]);
    }
    else { usage(); return 1; }
    return 0;
}