#pragma once
// ============================================================
//  bitmap.h  –  Free-space management via bitmaps
//  Each bit represents one inode or one data block.
//  0 = free, 1 = allocated.
// ============================================================
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include "fs_structures.h"

class Bitmap {
public:
    // ── Construction ─────────────────────────────────────────
    explicit Bitmap(uint32_t num_bits)
        : num_bits_(num_bits),
          num_bytes_((num_bits + 7) / 8),
          data_(new uint8_t[num_bytes_]())  // zero-initialised
    {}

    ~Bitmap() { delete[] data_; }
    Bitmap(const Bitmap&)            = delete;
    Bitmap& operator=(const Bitmap&) = delete;

    // ── Core operations ──────────────────────────────────────

    /// Mark bit at [index] as allocated (1).
    void set(uint32_t index) {
        validate(index);
        data_[index / 8] |= (1u << (index % 8));
    }

    /// Mark bit at [index] as free (0).
    void clear(uint32_t index) {
        validate(index);
        data_[index / 8] &= ~(1u << (index % 8));
    }

    /// Returns true if bit is allocated.
    bool test(uint32_t index) const {
        validate(index);
        return (data_[index / 8] >> (index % 8)) & 1u;
    }

    /// Find and allocate the first free bit.
    /// Returns the index or UINT32_MAX if none available.
    uint32_t allocate_first_free() {
        for (uint32_t i = 0; i < num_bits_; ++i) {
            if (!test(i)) {
                set(i);
                return i;
            }
        }
        return UINT32_MAX;
    }

    /// Count of free (0) bits.
    uint32_t free_count() const {
        uint32_t count = 0;
        for (uint32_t i = 0; i < num_bits_; ++i)
            if (!test(i)) ++count;
        return count;
    }

    /// Raw access for disk I/O.
    uint8_t*       data()       { return data_; }
    const uint8_t* data() const { return data_; }
    uint32_t       bytes() const { return num_bytes_; }

    /// Reset all bits to 0 (used by fsck).
    void clear_all() { std::memset(data_, 0, num_bytes_); }

private:
    void validate(uint32_t index) const {
        if (index >= num_bits_)
            throw std::out_of_range("Bitmap index out of range");
    }

    uint32_t  num_bits_;
    uint32_t  num_bytes_;
    uint8_t*  data_;
};
