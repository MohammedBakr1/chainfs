# ChainFS

A blockchain-verified filesystem built on FUSE.
Every file is split into 4KB blocks, each block is
SHA-256 hashed and chained to the previous one.
Any tampering is detected instantly.

## Build

    sudo pacman -S fuse2 openssl pkg-config base-devel
    make

## Usage

    make mount
    echo "hello" > /tmp/chainfs_mnt/file.txt
    cat /tmp/chainfs_mnt/file.txt
    make umount
    ./chainfs-verify /tmp/chainfs_store file.txt
