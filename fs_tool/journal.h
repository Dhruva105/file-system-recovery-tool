#pragma once
// ============================================================
//  journal.h  –  Write-Ahead Journaling (Recovery Module)
//
//  How journaling prevents data corruption on crash:
//
//  NORMAL WRITE SEQUENCE (without journal):
//    1. Update inode (metadata)      → crash here = orphaned blocks
//    2. Update data bitmap            → crash here = double allocation
//    3. Write data block              → crash here = lost data
//
//  JOURNALED WRITE SEQUENCE:
//    1. Write JOURNAL_BEGIN record
//    2. Write all changes to the journal (circular log on disk)
//    3. Write JOURNAL_COMMIT record   ← atomic "point of no return"
//    4. Apply changes to actual disk locations (checkpoint)
//    5. Write JOURNAL_CHECKPOINT record
//
//  On crash recovery (fsck):
//    - If COMMIT exists  → replay journal → consistent state
//    - If no COMMIT      → discard incomplete journal → old consistent state
//
//  This is called "REDO journaling" (metadata + data journaling).
// ============================================================
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include "fs_structures.h"

// ── Journal record types ─────────────────────────────────────
enum class JournalRecordType : uint32_t {
    INVALID     = 0,
    BEGIN       = 1,   // Start of a transaction
    DATA_BLOCK  = 2,   // One block of data to write
    COMMIT      = 3,   // Transaction is fully logged (safe to apply)
    CHECKPOINT  = 4,   // Transaction applied to main disk (journal can be freed)
};

// ── On-disk journal record header ───────────────────────────
struct JournalRecord {
    JournalRecordType type;
    uint32_t          transaction_id;
    uint32_t          target_block;    // Which disk block this data belongs to
    uint32_t          data_length;     // Bytes of payload (≤ BLOCK_SIZE)
    uint8_t           data[BLOCK_SIZE]; // The actual block content
};

constexpr uint32_t JOURNAL_SIZE_BLOCKS = 32;    // 32 × 4KB = 128 KB journal
constexpr uint32_t MAX_JOURNAL_RECORDS = JOURNAL_SIZE_BLOCKS; // one record/block

// ── In-memory journal manager ────────────────────────────────
class Journal {
public:
    Journal() : next_txn_id_(1) {}

    // ── Begin a new transaction ──────────────────────────────
    uint32_t begin_transaction() {
        uint32_t id = next_txn_id_++;
        JournalRecord rec{};
        rec.type           = JournalRecordType::BEGIN;
        rec.transaction_id = id;
        pending_records_.push_back(rec);
        return id;
    }

    // ── Log a block write inside current transaction ─────────
    void log_block(uint32_t txn_id, uint32_t target_block,
                   const uint8_t* data, uint32_t len = BLOCK_SIZE) {
        JournalRecord rec{};
        rec.type           = JournalRecordType::DATA_BLOCK;
        rec.transaction_id = txn_id;
        rec.target_block   = target_block;
        rec.data_length    = len;
        std::memcpy(rec.data, data, std::min(len, (uint32_t)BLOCK_SIZE));
        pending_records_.push_back(rec);
    }

    // ── Commit: mark transaction as fully logged ─────────────
    void commit(uint32_t txn_id) {
        JournalRecord rec{};
        rec.type           = JournalRecordType::COMMIT;
        rec.transaction_id = txn_id;
        pending_records_.push_back(rec);
        committed_records_.insert(committed_records_.end(),
                                  pending_records_.begin(),
                                  pending_records_.end());
        pending_records_.clear();
    }

    // ── Checkpoint: mark transaction as applied ──────────────
    void checkpoint(uint32_t txn_id) {
        committed_records_.erase(
            std::remove_if(committed_records_.begin(), committed_records_.end(),
                [txn_id](const JournalRecord& r){ return r.transaction_id == txn_id; }),
            committed_records_.end());
    }

    // ── Abort: discard uncommitted transaction ───────────────
    void abort(uint32_t txn_id) {
        pending_records_.erase(
            std::remove_if(pending_records_.begin(), pending_records_.end(),
                [txn_id](const JournalRecord& r){ return r.transaction_id == txn_id; }),
            pending_records_.end());
    }

    // ── Recovery: return records needing replay ──────────────
    const std::vector<JournalRecord>& committed_records() const {
        return committed_records_;
    }

    bool has_pending() const { return !pending_records_.empty(); }

    void clear() {
        pending_records_.clear();
        committed_records_.clear();
    }

    std::string status() const {
        return "Journal: " + std::to_string(committed_records_.size())
             + " committed records, "
             + std::to_string(pending_records_.size())
             + " pending records.";
    }

private:
    uint32_t                  next_txn_id_;
    std::vector<JournalRecord> pending_records_;
    std::vector<JournalRecord> committed_records_;
};
