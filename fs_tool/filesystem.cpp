// ============================================================
//  filesystem.cpp  –  FileSystem implementation
// ============================================================
#include "filesystem.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <ctime>
#include <cassert>

// ─────────────────────────────────────────────────────────────
//  Constructor / Destructor
// ─────────────────────────────────────────────────────────────
FileSystem::FileSystem(const std::string& disk_path)
    : disk_(disk_path),
      inode_bm_(std::make_unique<Bitmap>(INODE_COUNT)),
      data_bm_(std::make_unique<Bitmap>(DATA_BLOCK_COUNT))
{
    read_superblock();
    if (!sb_.is_valid()) {
        std::cout << "[FS] No valid file system found. Formatting...\n";
        format();
    } else {
        // Mark file system as dirty (in-use) on mount
        sb_.fs_state = 2;
        write_superblock();
        read_bitmaps();
        // Replay journal in case of previous unclean shutdown
        if (sb_.fs_state == 2) {
            std::cout << "[FS] Unclean mount detected. Running recovery...\n";
            recover_from_journal();
        }
    }
}

FileSystem::~FileSystem() {
    // Mark clean on unmount
    sb_.fs_state = 1;
    write_superblock();
    write_bitmaps();
    disk_.flush_cache();
}

// ─────────────────────────────────────────────────────────────
//  Format
// ─────────────────────────────────────────────────────────────
void FileSystem::format() {
    // 1. Initialise and write superblock
    sb_.init();
    write_superblock();

    // 2. Zero out bitmaps
    inode_bm_->clear_all();
    data_bm_->clear_all();
    write_bitmaps();

    // 3. Zero out inode table blocks
    for (uint32_t b = INODE_TABLE_START; b < DATA_START_BLOCK; ++b)
        disk_.zero_block(b);

    // 4. Create root directory (inode 0)
    uint32_t root_id = alloc_inode();   // Should return 0
    assert(root_id == ROOT_INODE);
    Inode root_inode;
    root_inode.init(ROOT_INODE, FileType::DIRECTORY);
    root_inode.created_at  = static_cast<uint64_t>(std::time(nullptr));
    root_inode.modified_at = root_inode.created_at;
    // Allocate one data block for the root directory entries
    uint32_t dir_block = alloc_data_block();
    root_inode.direct[0] = dir_block;
    root_inode.file_size = BLOCK_SIZE;
    disk_.zero_block(dir_block);   // Empty directory

    write_inode(ROOT_INODE, root_inode);
    write_superblock();   // Update free counts
    write_bitmaps();

    std::cout << "[FS] Formatted. Disk: " << TOTAL_BLOCKS << " blocks × "
              << BLOCK_SIZE << " bytes = "
              << (TOTAL_BLOCKS * BLOCK_SIZE / 1024) << " KB\n";
}

// ─────────────────────────────────────────────────────────────
//  create_file
// ─────────────────────────────────────────────────────────────
uint32_t FileSystem::create_file(const std::string& name) {
    if (name.size() >= MAX_FILENAME)
        throw std::invalid_argument("Filename too long (max " + std::to_string(MAX_FILENAME-1) + " chars)");
    if (find_in_root(name) != 0)
        throw std::runtime_error("File already exists: " + name);

    // ── Journal transaction ──────────────────────────────────
    uint32_t txn = journal_.begin_transaction();

    uint32_t inode_id = alloc_inode();
    if (inode_id == UINT32_MAX) { journal_.abort(txn); throw std::runtime_error("No free inodes"); }

    Inode inode;
    inode.init(inode_id, FileType::REGULAR);
    inode.created_at  = static_cast<uint64_t>(std::time(nullptr));
    inode.modified_at = inode.created_at;

    write_inode(inode_id, inode);
    add_to_root(name, inode_id);
    write_superblock();
    write_bitmaps();

    journal_.commit(txn);
    journal_.checkpoint(txn);

    std::cout << "[FS] Created file '" << name << "' → inode " << inode_id << "\n";
    return inode_id;
}

// ─────────────────────────────────────────────────────────────
//  delete_file
// ─────────────────────────────────────────────────────────────
bool FileSystem::delete_file(const std::string& name) {
    uint32_t inode_id = find_in_root(name);
    if (inode_id == 0) {
        std::cout << "[FS] File not found: " << name << "\n";
        return false;
    }

    uint32_t txn = journal_.begin_transaction();

    Inode inode;
    read_inode(inode_id, inode);

    // Free all direct data blocks
    for (uint32_t i = 0; i < DIRECT_BLOCKS; ++i) {
        if (inode.direct[i] != 0) {
            free_data_block(inode.direct[i]);
            inode.direct[i] = 0;
        }
    }

    // Free indirect block (if any)
    if (inode.single_indirect != 0) {
        uint8_t indirect_buf[BLOCK_SIZE];
        disk_.read_block(inode.single_indirect, indirect_buf);
        uint32_t* ptrs = reinterpret_cast<uint32_t*>(indirect_buf);
        uint32_t num_ptrs = BLOCK_SIZE / sizeof(uint32_t);
        for (uint32_t i = 0; i < num_ptrs; ++i)
            if (ptrs[i] != 0) free_data_block(ptrs[i]);
        free_data_block(inode.single_indirect);
    }

    free_inode(inode_id);
    remove_from_root(name);
    write_superblock();
    write_bitmaps();

    journal_.commit(txn);
    journal_.checkpoint(txn);

    std::cout << "[FS] Deleted file '" << name << "'\n";
    return true;
}

// ─────────────────────────────────────────────────────────────
//  write_file
// ─────────────────────────────────────────────────────────────
void FileSystem::write_file(const std::string& name, const std::vector<uint8_t>& data) {
    uint32_t inode_id = find_in_root(name);
    if (inode_id == 0) throw std::runtime_error("File not found: " + name);

    uint32_t txn = journal_.begin_transaction();

    Inode inode;
    read_inode(inode_id, inode);

    // Free any existing data blocks first (truncate to zero)
    for (uint32_t i = 0; i < DIRECT_BLOCKS; ++i) {
        if (inode.direct[i]) { free_data_block(inode.direct[i]); inode.direct[i] = 0; }
    }

    // Write data in BLOCK_SIZE chunks
    uint32_t bytes_left    = static_cast<uint32_t>(data.size());
    uint32_t offset        = 0;
    uint32_t block_index   = 0;
    uint8_t  buf[BLOCK_SIZE];

    while (bytes_left > 0 && block_index < DIRECT_BLOCKS) {
        uint32_t blk = alloc_data_block();
        if (blk == UINT32_MAX) { journal_.abort(txn); throw std::runtime_error("Disk full"); }
        uint32_t chunk = std::min(bytes_left, (uint32_t)BLOCK_SIZE);
        std::memset(buf, 0, BLOCK_SIZE);
        std::memcpy(buf, data.data() + offset, chunk);
        disk_.write_block(blk, buf);
        // Log to journal
        journal_.log_block(txn, blk, buf);
        inode.direct[block_index++] = blk;
        offset     += chunk;
        bytes_left -= chunk;
    }

    // Handle indirect blocks for large files
    if (bytes_left > 0) {
        uint32_t indirect_blk = alloc_data_block();
        if (indirect_blk == UINT32_MAX) { journal_.abort(txn); throw std::runtime_error("Disk full"); }
        uint8_t  indirect_buf[BLOCK_SIZE] = {};
        uint32_t* ptrs = reinterpret_cast<uint32_t*>(indirect_buf);
        uint32_t  ptr_idx = 0;
        uint32_t  max_ptrs = BLOCK_SIZE / sizeof(uint32_t);

        while (bytes_left > 0 && ptr_idx < max_ptrs) {
            uint32_t blk = alloc_data_block();
            if (blk == UINT32_MAX) { journal_.abort(txn); throw std::runtime_error("Disk full"); }
            uint32_t chunk = std::min(bytes_left, (uint32_t)BLOCK_SIZE);
            std::memset(buf, 0, BLOCK_SIZE);
            std::memcpy(buf, data.data() + offset, chunk);
            disk_.write_block(blk, buf);
            journal_.log_block(txn, blk, buf);
            ptrs[ptr_idx++] = blk;
            offset     += chunk;
            bytes_left -= chunk;
        }
        disk_.write_block(indirect_blk, indirect_buf);
        inode.single_indirect = indirect_blk;
    }

    inode.file_size   = static_cast<uint32_t>(data.size());
    inode.modified_at = static_cast<uint64_t>(std::time(nullptr));
    write_inode(inode_id, inode);
    write_superblock();
    write_bitmaps();

    journal_.commit(txn);
    journal_.checkpoint(txn);

    std::cout << "[FS] Wrote " << data.size() << " bytes to '" << name << "'\n";
}

// ─────────────────────────────────────────────────────────────
//  read_file
// ─────────────────────────────────────────────────────────────
std::vector<uint8_t> FileSystem::read_file(const std::string& name) {
    uint32_t inode_id = find_in_root(name);
    if (inode_id == 0) throw std::runtime_error("File not found: " + name);

    Inode inode;
    read_inode(inode_id, inode);

    std::vector<uint8_t> result;
    result.reserve(inode.file_size);

    uint8_t  buf[BLOCK_SIZE];
    uint32_t bytes_left = inode.file_size;

    // Read direct blocks
    for (uint32_t i = 0; i < DIRECT_BLOCKS && bytes_left > 0; ++i) {
        if (!inode.direct[i]) break;
        disk_.read_block(inode.direct[i], buf);
        uint32_t chunk = std::min(bytes_left, (uint32_t)BLOCK_SIZE);
        result.insert(result.end(), buf, buf + chunk);
        bytes_left -= chunk;
    }

    // Read indirect blocks
    if (bytes_left > 0 && inode.single_indirect) {
        uint8_t indirect_buf[BLOCK_SIZE];
        disk_.read_block(inode.single_indirect, indirect_buf);
        uint32_t* ptrs = reinterpret_cast<uint32_t*>(indirect_buf);
        uint32_t  max_ptrs = BLOCK_SIZE / sizeof(uint32_t);
        for (uint32_t i = 0; i < max_ptrs && bytes_left > 0; ++i) {
            if (!ptrs[i]) break;
            disk_.read_block(ptrs[i], buf);
            uint32_t chunk = std::min(bytes_left, (uint32_t)BLOCK_SIZE);
            result.insert(result.end(), buf, buf + chunk);
            bytes_left -= chunk;
        }
    }

    return result;
}

// ─────────────────────────────────────────────────────────────
//  list_files
// ─────────────────────────────────────────────────────────────
std::vector<std::string> FileSystem::list_files() {
    Inode root;
    read_inode(ROOT_INODE, root);

    std::vector<std::string> names;
    uint8_t buf[BLOCK_SIZE];

    for (uint32_t bi = 0; bi < DIRECT_BLOCKS; ++bi) {
        if (!root.direct[bi]) break;
        disk_.read_block(root.direct[bi], buf);
        DirEntry* entries = reinterpret_cast<DirEntry*>(buf);
        for (uint32_t i = 0; i < DIRENTS_PER_BLOCK; ++i) {
            if (entries[i].inode_id != 0)
                names.emplace_back(entries[i].name);
        }
    }
    return names;
}

// ─────────────────────────────────────────────────────────────
//  simulate_crash
// ─────────────────────────────────────────────────────────────
void FileSystem::simulate_crash(const std::string& name) {
    uint32_t inode_id = find_in_root(name);
    if (inode_id == 0) throw std::runtime_error("File not found: " + name);
    disk_.simulate_crash(inode_id);
    std::cout << "[CRASH] Simulated crash: inode " << inode_id
              << " for '" << name << "' is now corrupted!\n";
}

// ─────────────────────────────────────────────────────────────
//  fsck  –  File System Consistency Check
// ─────────────────────────────────────────────────────────────
std::string FileSystem::fsck() {
    std::ostringstream report;
    report << "\n╔══════════════════════════════════════╗\n"
           << "║     FSCK - Consistency Checker       ║\n"
           << "╚══════════════════════════════════════╝\n\n";

    Bitmap  ref_inode_bm(INODE_COUNT);
    Bitmap  ref_data_bm(DATA_BLOCK_COUNT);
    uint8_t buf[BLOCK_SIZE];
    int     errors_found = 0, errors_fixed = 0;

    // ── Pass 1: Scan inode table ─────────────────────────────
    report << "[Pass 1] Scanning inode table...\n";
    for (uint32_t id = 0; id < INODE_COUNT; ++id) {
        try {
            Inode inode;
            read_inode(id, inode);
            if (!inode.is_used()) continue;

            // Inode appears used — mark it in our reference bitmap
            ref_inode_bm.set(id);

            // Check magic / type field
            if (inode.type != FileType::REGULAR && inode.type != FileType::DIRECTORY) {
                report << "  [ERROR] Inode " << id << " has invalid type byte "
                       << static_cast<int>(inode.type) << " — clearing\n";
                Inode blank{};
                write_inode(id, blank);
                ++errors_found; ++errors_fixed;
                continue;
            }

            // Verify each direct block pointer
            for (uint32_t i = 0; i < DIRECT_BLOCKS; ++i) {
                uint32_t blk = inode.direct[i];
                if (!blk) continue;
                if (blk < DATA_START_BLOCK || blk >= TOTAL_BLOCKS - JOURNAL_SIZE_BLOCKS) {
                    report << "  [ERROR] Inode " << id << " direct[" << i << "] = "
                           << blk << " is out of range — zeroing pointer\n";
                    inode.direct[i] = 0;
                    write_inode(id, inode);
                    ++errors_found; ++errors_fixed;
                } else {
                    uint32_t data_index = blk - DATA_START_BLOCK;
                    if (ref_data_bm.test(data_index)) {
                        report << "  [ERROR] Block " << blk
                               << " referenced by multiple inodes (double allocation)!\n";
                        ++errors_found;
                    } else {
                        ref_data_bm.set(data_index);
                    }
                }
            }
        } catch (...) {
            report << "  [WARNING] Could not read inode " << id << "\n";
        }
    }

    // ── Pass 2: Compare bitmaps ──────────────────────────────
    report << "[Pass 2] Comparing bitmaps...\n";
    for (uint32_t i = 0; i < INODE_COUNT; ++i) {
        bool on_disk = inode_bm_->test(i);
        bool computed = ref_inode_bm.test(i);
        if (on_disk != computed) {
            report << "  [MISMATCH] Inode bitmap[" << i << "]: disk=" << on_disk
                   << " computed=" << computed << " → fixing\n";
            if (computed) inode_bm_->set(i); else inode_bm_->clear(i);
            ++errors_found; ++errors_fixed;
        }
    }
    for (uint32_t i = 0; i < DATA_BLOCK_COUNT; ++i) {
        bool on_disk  = data_bm_->test(i);
        bool computed = ref_data_bm.test(i);
        if (on_disk != computed) {
            report << "  [MISMATCH] Data bitmap[" << i << "]: disk=" << on_disk
                   << " computed=" << computed << " → fixing\n";
            if (computed) data_bm_->set(i); else data_bm_->clear(i);
            ++errors_found; ++errors_fixed;
        }
    }

    // ── Persist fixes ────────────────────────────────────────
    if (errors_fixed > 0) {
        write_bitmaps();
        // Recompute free counts
        sb_.free_inodes = inode_bm_->free_count();
        sb_.free_blocks = data_bm_->free_count();
        sb_.fs_state    = 1;  // clean
        write_superblock();
    }

    report << "\n── Summary ──────────────────────────────\n"
           << "  Errors found : " << errors_found << "\n"
           << "  Errors fixed : " << errors_fixed << "\n"
           << "  FS state     : " << (errors_found == 0 ? "CLEAN ✓" : "REPAIRED ✓") << "\n";
    return report.str();
}

// ─────────────────────────────────────────────────────────────
//  recover_from_journal
// ─────────────────────────────────────────────────────────────
std::string FileSystem::recover_from_journal() {
    std::ostringstream report;
    const auto& records = journal_.committed_records();
    if (records.empty()) {
        report << "[Journal] Nothing to recover.\n";
        return report.str();
    }
    report << "[Journal] Replaying " << records.size() << " committed records...\n";
    for (const auto& rec : records) {
        if (rec.type == JournalRecordType::DATA_BLOCK) {
            disk_.write_block(rec.target_block, rec.data);
            report << "  Restored block " << rec.target_block << "\n";
        }
    }
    journal_.clear();
    sb_.fs_state = 1;
    write_superblock();
    report << "[Journal] Recovery complete. FS is clean.\n";
    return report.str();
}

// ─────────────────────────────────────────────────────────────
//  defragment
//  Algorithm: scan all inodes, collect their blocks, move them
//  to the lowest contiguous free positions. This simulates
//  a "copy-compact" defragmentation.
// ─────────────────────────────────────────────────────────────
std::string FileSystem::defragment() {
    std::ostringstream report;
    report << "\n╔══════════════════════════════════════╗\n"
           << "║      Disk Defragmentation            ║\n"
           << "╚══════════════════════════════════════╝\n\n";

    // Collect blocks used by each inode (in file order)
    struct FileBlocks { uint32_t inode_id; std::vector<uint32_t> blocks; };
    std::vector<FileBlocks> all_files;

    for (uint32_t id = 0; id < INODE_COUNT; ++id) {
        if (!inode_bm_->test(id)) continue;
        Inode inode;
        read_inode(id, inode);
        if (!inode.is_used() || inode.type != FileType::REGULAR) continue;

        FileBlocks fb; fb.inode_id = id;
        for (uint32_t i = 0; i < DIRECT_BLOCKS; ++i)
            if (inode.direct[i]) fb.blocks.push_back(inode.direct[i]);
        if (!fb.blocks.empty()) all_files.push_back(std::move(fb));
    }

    // Reset data bitmap and re-pack blocks from position 0
    data_bm_->clear_all();
    uint32_t next_free = DATA_START_BLOCK;
    uint8_t  buf[BLOCK_SIZE];

    for (auto& fb : all_files) {
        Inode inode;
        read_inode(fb.inode_id, inode);
        for (uint32_t bi = 0; bi < fb.blocks.size(); ++bi) {
            uint32_t old_blk = fb.blocks[bi];
            uint32_t new_blk = next_free++;
            if (old_blk != new_blk) {
                disk_.read_block(old_blk, buf);
                disk_.write_block(new_blk, buf);
                disk_.zero_block(old_blk);
                report << "  Block " << old_blk << " → " << new_blk << "\n";
            }
            data_bm_->set(new_blk - DATA_START_BLOCK);
            inode.direct[bi] = new_blk;
        }
        write_inode(fb.inode_id, inode);
    }

    sb_.free_blocks = data_bm_->free_count();
    write_bitmaps();
    write_superblock();

    report << "\nDefragmentation complete. "
           << "All file blocks are now contiguous.\n";
    return report.str();
}

// ─────────────────────────────────────────────────────────────
//  Print helpers
// ─────────────────────────────────────────────────────────────
void FileSystem::print_superblock() const {
    std::cout << "\n── Superblock ──────────────────────────\n"
              << "  Magic        : 0x" << std::hex << sb_.magic << std::dec << "\n"
              << "  Total blocks : " << sb_.total_blocks << "\n"
              << "  Block size   : " << sb_.block_size << " B\n"
              << "  Free blocks  : " << sb_.free_blocks << "\n"
              << "  Free inodes  : " << sb_.free_inodes << "\n"
              << "  FS state     : " << (sb_.fs_state == 1 ? "CLEAN" : "DIRTY") << "\n"
              << "────────────────────────────────────────\n";
}

void FileSystem::print_cache_stats() const {
    auto& c = const_cast<VirtualDisk&>(disk_).cache();
    std::cout << "\n── LRU Cache Stats ─────────────────────\n"
              << "  Hits         : " << c.hits() << "\n"
              << "  Misses       : " << c.misses() << "\n"
              << "  Hit rate     : " << c.hit_rate() << " %\n"
              << "────────────────────────────────────────\n";
}

// ─────────────────────────────────────────────────────────────
//  Private helpers
// ─────────────────────────────────────────────────────────────
uint32_t FileSystem::alloc_inode() {
    uint32_t id = inode_bm_->allocate_first_free();
    if (id != UINT32_MAX) --sb_.free_inodes;
    return id;
}

void FileSystem::free_inode(uint32_t id) {
    inode_bm_->clear(id);
    ++sb_.free_inodes;
    Inode blank{};
    write_inode(id, blank);
}

uint32_t FileSystem::alloc_data_block() {
    uint32_t idx = data_bm_->allocate_first_free();
    if (idx == UINT32_MAX) return UINT32_MAX;
    --sb_.free_blocks;
    return DATA_START_BLOCK + idx;
}

void FileSystem::free_data_block(uint32_t block_num) {
    uint32_t idx = block_num - DATA_START_BLOCK;
    data_bm_->clear(idx);
    disk_.zero_block(block_num);
    ++sb_.free_blocks;
}

void FileSystem::read_inode(uint32_t id, Inode& out) {
    constexpr uint32_t inodes_per_block = BLOCK_SIZE / sizeof(Inode);
    uint32_t block  = INODE_TABLE_START + id / inodes_per_block;
    uint32_t offset = id % inodes_per_block;
    uint8_t  buf[BLOCK_SIZE];
    disk_.read_block(block, buf);
    std::memcpy(&out, buf + offset * sizeof(Inode), sizeof(Inode));
}

void FileSystem::write_inode(uint32_t id, const Inode& in) {
    constexpr uint32_t inodes_per_block = BLOCK_SIZE / sizeof(Inode);
    uint32_t block  = INODE_TABLE_START + id / inodes_per_block;
    uint32_t offset = id % inodes_per_block;
    uint8_t  buf[BLOCK_SIZE];
    disk_.read_block(block, buf);
    std::memcpy(buf + offset * sizeof(Inode), &in, sizeof(Inode));
    disk_.write_block(block, buf);
}

void FileSystem::read_superblock() {
    uint8_t buf[BLOCK_SIZE];
    disk_.read_block(SUPERBLOCK_BLOCK, buf);
    std::memcpy(&sb_, buf, sizeof(Superblock));
}

void FileSystem::write_superblock() {
    uint8_t buf[BLOCK_SIZE] = {};
    std::memcpy(buf, &sb_, sizeof(Superblock));
    disk_.write_block(SUPERBLOCK_BLOCK, buf);
}

void FileSystem::read_bitmaps() {
    uint8_t buf[BLOCK_SIZE];
    disk_.read_block(INODE_BITMAP_BLOCK, buf);
    std::memcpy(inode_bm_->data(), buf, inode_bm_->bytes());
    disk_.read_block(DATA_BITMAP_BLOCK, buf);
    std::memcpy(data_bm_->data(), buf, data_bm_->bytes());
}

void FileSystem::write_bitmaps() {
    uint8_t buf[BLOCK_SIZE] = {};
    std::memcpy(buf, inode_bm_->data(), inode_bm_->bytes());
    disk_.write_block(INODE_BITMAP_BLOCK, buf);
    std::memset(buf, 0, BLOCK_SIZE);
    std::memcpy(buf, data_bm_->data(), data_bm_->bytes());
    disk_.write_block(DATA_BITMAP_BLOCK, buf);
}

uint32_t FileSystem::find_in_root(const std::string& name) {
    Inode root;
    read_inode(ROOT_INODE, root);
    uint8_t buf[BLOCK_SIZE];
    for (uint32_t bi = 0; bi < DIRECT_BLOCKS; ++bi) {
        if (!root.direct[bi]) continue;
        disk_.read_block(root.direct[bi], buf);
        DirEntry* entries = reinterpret_cast<DirEntry*>(buf);
        for (uint32_t i = 0; i < DIRENTS_PER_BLOCK; ++i) {
            if (entries[i].inode_id != 0 && name == entries[i].name)
                return entries[i].inode_id;
        }
    }
    return 0;
}

bool FileSystem::add_to_root(const std::string& name, uint32_t inode_id) {
    Inode root;
    read_inode(ROOT_INODE, root);
    uint8_t buf[BLOCK_SIZE];
    for (uint32_t bi = 0; bi < DIRECT_BLOCKS; ++bi) {
        if (!root.direct[bi]) {
            // Allocate new block for the directory
            uint32_t blk = alloc_data_block();
            if (blk == UINT32_MAX) return false;
            root.direct[bi] = blk;
            disk_.zero_block(blk);
            write_inode(ROOT_INODE, root);
        }
        disk_.read_block(root.direct[bi], buf);
        DirEntry* entries = reinterpret_cast<DirEntry*>(buf);
        for (uint32_t i = 0; i < DIRENTS_PER_BLOCK; ++i) {
            if (entries[i].inode_id == 0) {
                entries[i].inode_id = inode_id;
                std::strncpy(entries[i].name, name.c_str(), MAX_FILENAME - 1);
                entries[i].name[MAX_FILENAME - 1] = '\0';
                disk_.write_block(root.direct[bi], buf);
                return true;
            }
        }
    }
    return false;
}

bool FileSystem::remove_from_root(const std::string& name) {
    Inode root;
    read_inode(ROOT_INODE, root);
    uint8_t buf[BLOCK_SIZE];
    for (uint32_t bi = 0; bi < DIRECT_BLOCKS; ++bi) {
        if (!root.direct[bi]) continue;
        disk_.read_block(root.direct[bi], buf);
        DirEntry* entries = reinterpret_cast<DirEntry*>(buf);
        for (uint32_t i = 0; i < DIRENTS_PER_BLOCK; ++i) {
            if (entries[i].inode_id != 0 && name == entries[i].name) {
                entries[i].inode_id = 0;
                std::memset(entries[i].name, 0, MAX_FILENAME);
                disk_.write_block(root.direct[bi], buf);
                return true;
            }
        }
    }
    return false;
}
