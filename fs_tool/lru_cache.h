#pragma once
// ============================================================
//  lru_cache.h  –  LRU Block Cache (Optimization Layer)
//
//  Why?  Disk I/O is ~1000× slower than RAM.  By caching
//  recently-used blocks we avoid redundant disk reads.
//
//  Algorithm:
//    - Keep a doubly-linked list ordered by recency.
//    - Keep a hash map: block_number → list iterator.
//    - On GET: move the entry to the front (most-recent).
//    - On PUT: insert at front; if over capacity, evict from back.
//  Time complexity: O(1) per get/put.
// ============================================================
#include <cstdint>
#include <cstring>
#include <list>
#include <unordered_map>
#include <vector>
// #include <optional>
#include "fs_structures.h"

class LRUCache {
public:
    struct CacheEntry {
        uint32_t block_num;
        uint8_t  data[BLOCK_SIZE];
        bool     dirty;   // true = needs write-back to disk
    };

    explicit LRUCache(uint32_t capacity) : capacity_(capacity) {}

    // ── Get a cached block (returns nullptr if miss) ─────────
    uint8_t* get(uint32_t block_num) {
        auto it = map_.find(block_num);
        if (it == map_.end()) {
            ++misses_;
            return nullptr;
        }
        // Move to front (most recently used)
        list_.splice(list_.begin(), list_, it->second);
        ++hits_;
        return it->second->data;
    }

    // ── Insert / update a block in cache ─────────────────────
    void put(uint32_t block_num, const uint8_t* data, bool dirty = false) {
        auto it = map_.find(block_num);
        if (it != map_.end()) {
            list_.splice(list_.begin(), list_, it->second);
            std::memcpy(it->second->data, data, BLOCK_SIZE);
            it->second->dirty = dirty;
            return;
        }
        if (list_.size() >= capacity_) evict();
        list_.push_front({block_num, {}, dirty});
        std::memcpy(list_.front().data, data, BLOCK_SIZE);
        map_[block_num] = list_.begin();
    }

    // ── Mark a cached block dirty (modified in memory) ───────
    void mark_dirty(uint32_t block_num) {
        auto it = map_.find(block_num);
        if (it != map_.end()) it->second->dirty = true;
    }

    // ── Collect all dirty entries for write-back ─────────────
    std::vector<CacheEntry> dirty_entries() const {
        std::vector<CacheEntry> result;
        for (const auto& e : list_)
            if (e.dirty) result.push_back(e);
        return result;
    }

    void invalidate(uint32_t block_num) {
        auto it = map_.find(block_num);
        if (it == map_.end()) return;
        list_.erase(it->second);
        map_.erase(it);
    }

    void clear() { list_.clear(); map_.clear(); }

    // ── Statistics ───────────────────────────────────────────
    uint64_t hits()   const { return hits_; }
    uint64_t misses() const { return misses_; }
    double   hit_rate() const {
        uint64_t total = hits_ + misses_;
        return total ? (double)hits_ / total * 100.0 : 0.0;
    }

private:
    void evict() {
        auto& back = list_.back();
        // Caller (VirtualDisk) handles dirty write-back via dirty_entries()
        map_.erase(back.block_num);
        list_.pop_back();
        ++evictions_;
    }

    uint32_t  capacity_;
    std::list<CacheEntry>                              list_;
    std::unordered_map<uint32_t, std::list<CacheEntry>::iterator> map_;
    uint64_t  hits_      = 0;
    uint64_t  misses_    = 0;
    uint64_t  evictions_ = 0;
};
