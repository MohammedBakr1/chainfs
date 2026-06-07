CC      = gcc
CFLAGS  = -Wall -Wextra -g $(shell pkg-config fuse --cflags)
LDFLAGS = $(shell pkg-config fuse --libs) -lssl -lcrypto -lpthread

SRC = src/main.c src/fuse_ops.c src/block_mgr.c src/merkle.c src/storage.c src/wal.c src/crypto.c src/network.c
OBJ = $(SRC:.c=.o)

all: chainfs chainfs-verify chainfs-cli

chainfs: $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

chainfs-verify: src/verify.o src/block_mgr.o src/merkle.o src/storage.o src/wal.o src/crypto.o
	$(CC) -o $@ $^ -lssl -lcrypto

chainfs-cli: src/cli.o
	$(CC) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

clean:
	rm -f $(OBJ) src/verify.o src/cli.o chainfs chainfs-verify chainfs-cli

mount:
	mkdir -p /tmp/chainfs_store /tmp/chainfs_mnt
	./chainfs /tmp/chainfs_store /tmp/chainfs_mnt

umount:
	fusermount -u /tmp/chainfs_mnt