#pragma once
// ============================================================
//  virtual_disk.h  –  Low-level disk I/O layer
//  Wraps a binary file to simulate a real block device.
//  All higher layers talk to this via read_block/write_block.
// ============================================================
#include <string>
#include <fstream>
#include <stdexcept>
#include <memory>
#include "fs_structures.h"
#include "lru_cache.h"

class VirtualDisk {
public:
    // ── Open or create a virtual disk file ───────────────────
    explicit VirtualDisk(const std::string& path,
                         uint32_t cache_capacity = 64)
        : path_(path), cache_(cache_capacity)
    {
        // Try to open existing disk
        file_.open(path, std::ios::in | std::ios::out | std::ios::binary);
        if (!file_.is_open()) {
            // Create new disk file filled with zeros
            std::ofstream create(path, std::ios::out | std::ios::binary);
            if (!create) throw std::runtime_error("Cannot create disk file: " + path);
            std::vector<uint8_t> zeros(BLOCK_SIZE, 0);
            for (uint32_t i = 0; i < TOTAL_BLOCKS; ++i)
                create.write(reinterpret_cast<char*>(zeros.data()), BLOCK_SIZE);
            create.close();
            file_.open(path, std::ios::in | std::ios::out | std::ios::binary);
            if (!file_.is_open())
                throw std::runtime_error("Cannot open disk file after creation: " + path);
        }
    }

    ~VirtualDisk() { flush_cache(); }

    // ── Read a 4 KB block from disk (cache-backed) ───────────
    void read_block(uint32_t block_num, uint8_t* buf) {
        validate_block(block_num);
        uint8_t* cached = cache_.get(block_num);
        if (cached) {
            std::memcpy(buf, cached, BLOCK_SIZE);
            return;
        }
        // Cache miss → read from disk
        file_.seekg(static_cast<std::streamoff>(block_num) * BLOCK_SIZE, std::ios::beg);
        file_.read(reinterpret_cast<char*>(buf), BLOCK_SIZE);
        if (!file_) throw std::runtime_error("Disk read failed at block " + std::to_string(block_num));
        cache_.put(block_num, buf);
    }

    // ── Write a 4 KB block to disk (write-through) ───────────
    void write_block(uint32_t block_num, const uint8_t* buf) {
        validate_block(block_num);
        file_.seekp(static_cast<std::streamoff>(block_num) * BLOCK_SIZE, std::ios::beg);
        file_.write(reinterpret_cast<const char*>(buf), BLOCK_SIZE);
        file_.flush();
        if (!file_) throw std::runtime_error("Disk write failed at block " + std::to_string(block_num));
        cache_.put(block_num, buf);
    }

    // ── Zero-fill a block ────────────────────────────────────
    void zero_block(uint32_t block_num) {
        static const uint8_t zeros[BLOCK_SIZE] = {};
        write_block(block_num, zeros);
        cache_.invalidate(block_num);
    }

    // ── Simulate crash: corrupt specific metadata blocks ─────
    void simulate_crash(uint32_t inode_id) {
        // Write garbage bytes into the inode table to simulate crash-corruption
        uint8_t corrupt[BLOCK_SIZE];
        for (uint32_t i = 0; i < BLOCK_SIZE; ++i)
            corrupt[i] = static_cast<uint8_t>(i * 37 + 0xAB); // pseudo-random garbage
        uint32_t inode_block = INODE_TABLE_START + (inode_id / (BLOCK_SIZE / sizeof(Inode)));
        write_block(inode_block, corrupt);
        cache_.invalidate(inode_block);
    }

    // ── Flush all dirty cache entries to disk ────────────────
    void flush_cache() {
        for (auto& entry : cache_.dirty_entries()) {
            file_.seekp(static_cast<std::streamoff>(entry.block_num) * BLOCK_SIZE, std::ios::beg);
            file_.write(reinterpret_cast<char*>(entry.data), BLOCK_SIZE);
        }
        file_.flush();
    }

    LRUCache& cache() { return cache_; }

private:
    void validate_block(uint32_t block_num) const {
        if (block_num >= TOTAL_BLOCKS)
            throw std::out_of_range("Block number out of range: " + std::to_string(block_num));
    }

    std::string  path_;
    std::fstream file_;
    LRUCache     cache_;
};
