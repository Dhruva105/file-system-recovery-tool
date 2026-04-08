#pragma once
// ============================================================
//  fs_structures.h  –  Core on-disk data structures
//  File System Recovery & Optimization Tool
// ============================================================
#include <cstdint>
#include <cstring>
#include <array>

// ── Disk geometry ────────────────────────────────────────────
constexpr uint32_t BLOCK_SIZE       = 4096;          // 4 KB per block
constexpr uint32_t TOTAL_BLOCKS     = 1024;          // 4 MB virtual disk
constexpr uint32_t INODE_COUNT      = 128;
constexpr uint32_t DIRECT_BLOCKS    = 12;
constexpr uint32_t MAGIC_NUMBER     = 0xDEADBEEF;   // Superblock signature

// ── Block layout ─────────────────────────────────────────────
//  Block 0   : Superblock
//  Block 1   : Inode Bitmap  (1 bit per inode)
//  Block 2   : Data Bitmap   (1 bit per data block)
//  Blocks 3–10: Inode Table  (INODE_COUNT inodes)
//  Blocks 11+: Data Blocks
constexpr uint32_t SUPERBLOCK_BLOCK     = 0;
constexpr uint32_t INODE_BITMAP_BLOCK   = 1;
constexpr uint32_t DATA_BITMAP_BLOCK    = 2;
constexpr uint32_t INODE_TABLE_START    = 3;
constexpr uint32_t INODE_TABLE_BLOCKS   = 8;
constexpr uint32_t DATA_START_BLOCK     = INODE_TABLE_START + INODE_TABLE_BLOCKS; // 11
constexpr uint32_t DATA_BLOCK_COUNT     = TOTAL_BLOCKS - DATA_START_BLOCK;        // 1013

// ── File types ───────────────────────────────────────────────
enum class FileType : uint8_t { NONE = 0, REGULAR = 1, DIRECTORY = 2 };

// ────────────────────────────────────────────────────────────
//  SUPERBLOCK  (lives in Block 0)
//  The "birth certificate" of the entire file system.
// ────────────────────────────────────────────────────────────
struct Superblock {
    uint32_t magic;               // Must equal MAGIC_NUMBER
    uint32_t total_blocks;
    uint32_t data_block_count;
    uint32_t inode_count;
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint32_t block_size;
    uint32_t inode_table_start;
    uint32_t data_start;
    uint32_t journal_block;       // First block of the journal log
    uint8_t  fs_state;            // 1 = clean, 2 = dirty (unclean mount)
    uint8_t  _pad[BLOCK_SIZE - 41]; // Pad to full block

    void init() {
        magic            = MAGIC_NUMBER;
        total_blocks     = TOTAL_BLOCKS;
        data_block_count = DATA_BLOCK_COUNT;
        inode_count      = INODE_COUNT;
        free_blocks      = DATA_BLOCK_COUNT;
        free_inodes      = INODE_COUNT;
        block_size       = BLOCK_SIZE;
        inode_table_start= INODE_TABLE_START;
        data_start       = DATA_START_BLOCK;
        journal_block    = TOTAL_BLOCKS - 32; // Last 32 blocks = journal
        fs_state         = 1;
        std::memset(_pad, 0, sizeof(_pad));
    }
    bool is_valid() const { return magic == MAGIC_NUMBER; }
};
static_assert(sizeof(Superblock) == BLOCK_SIZE, "Superblock must fit in one block");

// ────────────────────────────────────────────────────────────
//  INODE  (Index Node)
//  Holds all metadata about a file. Does NOT store the name
//  (names live in directory entries).
// ────────────────────────────────────────────────────────────
struct Inode {
    uint32_t  inode_id;
    FileType  type;
    uint8_t   permissions;      // rwx bits
    uint32_t  file_size;        // bytes
    uint32_t  link_count;
    uint64_t  created_at;
    uint64_t  modified_at;
    uint32_t  direct[DIRECT_BLOCKS];   // 12 direct block pointers
    uint32_t  single_indirect;         // Points to a block of pointers
    uint8_t   _pad[BLOCK_SIZE/8 - sizeof(uint32_t)*(DIRECT_BLOCKS+1)
                               - sizeof(uint32_t)*3 - sizeof(uint64_t)*2
                               - sizeof(uint32_t) - sizeof(FileType)
                               - sizeof(uint8_t)];
    void init(uint32_t id, FileType t) {
        std::memset(this, 0, sizeof(*this));
        inode_id   = id;
        type       = t;
        permissions= 0b110; // rw-
        link_count = 1;
    }
    bool is_used() const { return type != FileType::NONE; }
};

// ────────────────────────────────────────────────────────────
//  DIRECTORY ENTRY
//  Maps a filename → inode_id inside a directory data block.
// ────────────────────────────────────────────────────────────
constexpr uint32_t MAX_FILENAME = 28;
constexpr uint32_t DIRENTS_PER_BLOCK = BLOCK_SIZE / 32;

struct DirEntry {
    uint32_t inode_id;           // 0 = unused slot
    char     name[MAX_FILENAME]; // null-terminated filename
};
static_assert(sizeof(DirEntry) == 32, "DirEntry must be 32 bytes");
