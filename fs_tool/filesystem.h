#pragma once
// ============================================================
//  filesystem.h  –  Main file system layer
//  Orchestrates: VirtualDisk + Bitmap + Inode + Journal
// ============================================================
#include <string>
#include <vector>
#include <ctime>
#include <stdexcept>
#include <algorithm>
#include <memory>
#include "fs_structures.h"
#include "virtual_disk.h"
#include "bitmap.h"
#include "journal.h"

class FileSystem {
public:
    // ── Mount / Format ───────────────────────────────────────
    explicit FileSystem(const std::string& disk_path);
    ~FileSystem();

    /// Format: wipe and re-initialise all metadata.
    void format();

    // ── Core File Operations ─────────────────────────────────
    /// Create a new regular file. Returns its inode id.
    uint32_t create_file(const std::string& name);

    /// Delete a file by name (unlink → free inode + data blocks).
    bool delete_file(const std::string& name);

    /// Write bytes to a file (overwrites from offset 0).
    void write_file(const std::string& name, const std::vector<uint8_t>& data);

    /// Read all bytes of a file.
    std::vector<uint8_t> read_file(const std::string& name);

    /// List all files in the root directory.
    std::vector<std::string> list_files();

    // ── Recovery Module ──────────────────────────────────────
    /// Simulate a crash by corrupting inode metadata.
    void simulate_crash(const std::string& name);

    /// File-system consistency check (like Linux fsck).
    /// Scans inodes vs bitmaps and repairs inconsistencies.
    std::string fsck();

    /// Replay journal to recover committed-but-not-checkpointed writes.
    std::string recover_from_journal();

    // ── Optimisation Module ──────────────────────────────────
    /// Defragment data blocks: relocate file blocks to be contiguous.
    std::string defragment();

    // ── Statistics / Info ────────────────────────────────────
    void print_superblock() const;
    void print_cache_stats() const;

private:
    // ── Helpers ──────────────────────────────────────────────
    uint32_t alloc_inode();
    void     free_inode(uint32_t id);
    uint32_t alloc_data_block();
    void     free_data_block(uint32_t block_num);

    void     read_inode(uint32_t id, Inode& out);
    void     write_inode(uint32_t id, const Inode& in);

    void     read_superblock();
    void     write_superblock();
    void     read_bitmaps();
    void     write_bitmaps();

    // Find directory entry for filename in root dir, return inode id or 0
    uint32_t find_in_root(const std::string& name);
    // Add a directory entry to root
    bool     add_to_root(const std::string& name, uint32_t inode_id);
    // Remove a directory entry from root
    bool     remove_from_root(const std::string& name);

    VirtualDisk              disk_;
    Superblock               sb_;
    std::unique_ptr<Bitmap>  inode_bm_;   // 1 bit per inode
    std::unique_ptr<Bitmap>  data_bm_;    // 1 bit per data block
    Journal                  journal_;

    // Root directory inode id (always 0)
    static constexpr uint32_t ROOT_INODE = 0;
};
