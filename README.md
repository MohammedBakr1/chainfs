# ChainFS: A Blockchain-Verified Encrypted Filesystem
### Integrity Verification, Per-Block AES-256 Encryption, and P2P Replication over FUSE

ChainFS is a userspace filesystem implemented in C on top of FUSE (Filesystem in Userspace) that provides cryptographic integrity verification, per-block AES-256-CBC encryption, and peer-to-peer block replication. It integrates these security components transparently at the block level, allowing standard applications to interact with the filesystem using traditional POSIX system calls without any modification.

---

## Architecture and System Design

ChainFS is structured into a clean, six-layer architecture where each layer maintains a single, distinct responsibility:

```
      POSIX Application Interface (open, read, write, stat)
                             │
                             ▼
               Linux VFS + FUSE Kernel Module
                             │
                             ▼
  ┌─────────────────────────────────────────────────────────┐
  │ L1: FUSE Callbacks (fuse_ops.c)                         │ ──► Implements 17 POSIX operations
  └──────────────────────────┬──────────────────────────────┘
                             ▼
  ┌─────────────────────────────────────────────────────────┐
  │ L2: Block Manager (block_mgr.c)                         │ ──► Amortized O(1) Inode Lookup
  └──────────────────────────┬──────────────────────────────┘
                             ▼
  ┌─────────────────────────────────────────────────────────┐
  │ L3: Hash Chain + Merkle (merkle.c)                      │ ──► Computes SHA-256 Hash Chain
  └──────────────────────────┬──────────────────────────────┘
                             ▼
  ┌─────────────────────────────────────────────────────────┐
  │ L4: AES-256 Encryption (crypto.c)                       │ ──► Per-block AES-256-CBC
  └──────────────────────────┬──────────────────────────────┘
                             ▼
  ┌─────────────────────────────────────────────────────────┐
  │ L5: Storage + WAL (storage.c, wal.c)                    │ ──► Write-Ahead Log for crash consistency
  └──────────────────────────┬──────────────────────────────┘
                             ▼
  ┌─────────────────────────────────────────────────────────┐
  │ P2P Replication Layer (network.c)                       │ ──► TCP block distribution (Port 8080)
  └─────────────────────────────────────────────────────────┘
```

### Core Data Structures

#### 1. Inode Structure
The central metadata structure stores essential file attributes, permissions, and integrity data:
```c
typedef struct {
    uint32_t        inode_id;
    uint32_t        parent_id;
    uint8_t         type;               // FILE/DIR/SYMLINK
    uint32_t        nlink;              // Hard link count
    uint32_t        size;               // Bytes
    uint32_t        block_count;
    uint32_t        block_ids[262144];  // 1GB max file size
    chainfs_hash_t  root_hash;          // Merkle root
    struct timespec atime, mtime, ctime;
    char            name[255];
    char            symlink_target[512];
} chainfs_inode_t;
```

#### 2. Global State
Tracks operational directories, key material, and allocation counters:
```c
typedef struct {
    char     storage_path[512];
    uint32_t next_inode_id;
    uint32_t next_block_id;
    uint8_t  enc_key[32];       // AES-256 key
    uint8_t  encrypted;         // Enabled flag
} chainfs_state_t;
```

---

## Features and POSIX Compliance

ChainFS implements 17 FUSE operations to provide compliance for standard file and directory operations:

| Operation | Syscall | Description |
| :--- | :--- | :--- |
| `getattr` | `stat()` | Fetches file attributes and metadata. |
| `readdir` | `getdents()` | Reads directory contents and child listings. |
| `mkdir` / `rmdir` | `mkdir()` / `rmdir()` | Creates or removes subdirectories. |
| `create` / `open` | `open(O_CREAT)` / `open()` | Handles file creation and descriptive file descriptor access. |
| `read` / `write` | `read()` / `write()` | Standard data payload read and write operations. |
| `symlink` / `readlink` | `symlink()` / `readlink()` | Creates and reads symbolic links. |
| `link` | `link()` | Handles hard link references to existing inodes. |
| `truncate` | `truncate()` | Adjusts file size allocations on disk. |
| `chmod` / `chown` | `chmod()` / `chown()` | Validates standard Unix access control permissions. |

---

## Technical Mechanisms

### 1. Cryptographic Integrity Chain
Files are partitioned into fixed-size 4KB blocks (B_0, B_1, ..., B_n-1). A SHA-256 hash chain is calculated iteratively across consecutive blocks:
* First block hash: h_0 = SHA256(0^32 || B_0)
* Intermediate block hashes: h_i = SHA256(h_i-1 || B_i) for i = 1, ..., n-1
* Merkle Root: r = SHA256(h_0 || h_1 || ... || h_n-1)

The final Merkle root r is securely written into `inode.root_hash` at write time. Because of the avalanche effect and collision resistance properties of SHA-256, any single-bit modification inside a block causes a full cascade error that results in a fundamentally different root hash, guaranteeing immediate tamper detection.

### 2. Encryption Design
* **Key Derivation:** The user-supplied mount password is mapped to a 256-bit key via k = SHA256(password).
* **Per-Block Cipher:** For every discrete block write, a fresh 128-bit random Initialization Vector (IV) is generated via OpenSSL's `RAND_bytes()`. The block is encrypted using AES-256-CBC, and saved to storage in a `[size (4B) | IV (16B) | Ciphertext]` format.
* **Pipeline Separation:** Hashing operates entirely on plaintext blocks (before encryption during writes, and after decryption during reads). This design pattern ensures integrity verification operations function independently of whether storage encryption is actively enabled or bypassed.

### 3. Crash Consistency (WAL)
To prevent inconsistent data states or corrupt file metadata if a crash happens mid-write, ChainFS processes writes through a transient Write-Ahead Log (WAL):
1. Append an entry to the WAL file with the state marked as `PENDING`.
2. Write the actual target block to the storage file layout.
3. Advance the WAL entry state indicator to `COMMITTED`.
4. Update the related file inode metadata and global tracking states on disk.
5. Unlink and purge the transient WAL log entry.

On mount, `wal_recover()` parses for residual `PENDING` identifiers and automatically re-executes interrupted operations to avoid data dropouts.

### 4. P2P Replication Protocol
Nodes communicate over standard TCP on port 8080 using target peer sets configured via `peers.conf` at initialization. Block operations emit binary network packets featuring strict header parameters:
```c
typedef struct {
    uint8_t  type;          // MSG_WRITE_BLOCK, MSG_READ_BLOCK, etc.
    uint32_t size;          // Payload size
    uint32_t block_id;
    uint8_t  checksum[32];
} __attribute__((packed)) net_msg_header_t;
```
Block writes stream the binary payload out to connected nodes, waiting for definitive acknowledgment frames (`MSG_ACK`) to ensure network replication parity.

---

## Storage Layout

The primary target directory specified by `storage_path` uses the following disk layout:
* `state`: Stored global 8-byte state containing `next_inode_id` and `next_block_id`.
* `.key`: Explicit 32-byte file saving the derived user AES key (only exists when encryption is configured).
* `wal.log`: Active transient logs mapping transactions currently passing through the write pipeline.
* `inode_NNNNN`: Serialized representations of custom binary `chainfs_inode_t` metadata records.
* `block_NNNNN`: Individual block files containing either `[size | IV + ciphertext]` or plaintext payloads.
* `peers.conf`: Standard flat-text mapping containing IP addresses and port pairs of cluster nodes.

---

## Installation and Usage Instructions

### Prerequisites
Install FUSE development packages and OpenSSL development tools (Arch Linux example):
```bash
sudo pacman -S fuse2 openssl pkg-config base-devel
```

### Build Instructions
```bash
git clone https://github.com/MohammedBakr1/ChainFS
cd ChainFS
make
```

### Mounting the Daemon
```bash
./chainfs /var/chainfs/store /mnt/chainfs
```
*The system will prompt for a passphrase. Providing an empty input maps operations directly to unencrypted plaintext storage profiles.*

### Direct Verification Check
```bash
./chainfs-verify /var/chainfs/store record.txt
```

### Peer Cluster Mapping Example
To establish block replication to a secondary cluster node, configure the peer descriptor map inside your storage file configuration before execution:
```bash
echo "192.168.1.100 8080" >> /var/chainfs/store/peers.conf
```

---

## Known System Limitations

* **Password Security KDF:** Key derivation depends directly on simple `SHA-256(password)` iterations instead of robust hashing standards like PBKDF2 or Argon2.
* **File Allocation Limit:** Maximum upper bound allocations are strictly locked to 1 GB due to static block tracking array limits (`block_ids[262144]`) inside the core Inode structure.
* **Consensus Engine:** The integrated P2P synchronization architecture transfers block files directly without handling independent consensus logic, making deployments vulnerable to split-brain consistency states.
* **Threading Execution Model:** FUSE bindings run on a single-threaded loop model, meaning active operations execute serially.

---

## Roadmap

- [ ] Transition key derivation layers to password-based Argon2 hashing standards.
- [ ] Incorporate indirect block pointers within file structures to support files larger than 1 GB.
- [ ] Implement a Raft consensus architecture to address multi-node consistency issues.
- [ ] Reimplement core hashing, parsing, and cryptographic sub-layers using Rust to ensure memory safety.

---

## License
This project is open-source software licensed under the terms of the MIT License.
