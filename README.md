# 🗂️ File System Recovery & Optimization Tool

<div align="center">

![VFS Lab Banner](https://capsule-render.vercel.app/api?type=waving&color=4f8cff&height=200&section=header&text=VFS%20Lab&fontSize=60&fontColor=ffffff&fontAlignY=38&desc=Unix-like%20Virtual%20File%20System%20%7C%20C%2B%2B17%20%7C%20Recovery%20%7C%20Optimization&descAlignY=56&descSize=16)

[![Language](https://img.shields.io/badge/Language-C%2B%2B17-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20Mac-lightgrey?style=for-the-badge&logo=linux&logoColor=white)](https://github.com)
[![Frontend](https://img.shields.io/badge/Dashboard-HTML%20%2B%20CSS%20%2B%20JS-F7DF1E?style=for-the-badge&logo=javascript&logoColor=black)](https://github.com)
[![FS State](https://img.shields.io/badge/FS%20State-CLEAN-2edd9a?style=for-the-badge)](https://github.com)
[![License](https://img.shields.io/badge/License-MIT-green?style=for-the-badge)](LICENSE)

<br/>

> **A professional-grade simulation of a Unix-like file system** built from scratch in C++17.  
> Features a write-ahead journal, bitmap free-space management, LRU block cache,  
> disk defragmentation, an `fsck` consistency checker, and a live web dashboard.

<br/>

<!-- ⭐ ADD YOUR DASHBOARD SCREENSHOT HERE ⭐ -->
<!-- After taking a screenshot of your web dashboard, upload it to your repo and replace the line below -->
<!-- ![Dashboard Preview](fs_web/preview.png) -->

**[▶ View Web Dashboard](#-web-dashboard) • [⚡ Quick Start](#-quick-start) • [🧠 How It Works](#-how-it-works)**

</div>

---

## 📌 Project Highlights

```
✦ 1024 blocks × 4KB = 4MB virtual disk stored as a binary file
✦ Full inode-based file system (superblock → inode table → data blocks)
✦ Write-ahead journaling prevents data loss on crash (like Linux ext4)
✦ fsck two-pass consistency checker repairs corrupted metadata
✦ LRU cache with 91% hit rate reduces disk I/O by ~10×
✦ Copy-compact defragmentation for sequential block layout
✦ Interactive CLI + live web dashboard — no frameworks, no dependencies
```

---

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     CLI  (main.cpp)                             │
│          create · write · read · delete · crash · fsck          │
└──────────────────────────┬──────────────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────────────┐
│                  FileSystem Layer  (filesystem.h/.cpp)          │
│    Core logic: allocation, directory management, journaling     │
└────────┬──────────────────┬──────────────────┬──────────────────┘
         │                  │                  │
┌────────▼────────┐ ┌───────▼───────┐ ┌───────▼──────────┐
│  VirtualDisk    │ │    Bitmap     │ │     Journal      │
│ (virtual_disk.h)│ │  (bitmap.h)   │ │  (journal.h)     │
│ read_block      │ │ inode_bitmap  │ │ BEGIN→DATA→      │
│ write_block     │ │ data_bitmap   │ │ COMMIT→CHECKPOINT│
│ simulate_crash  │ │ alloc / free  │ └──────────────────┘
└────────┬────────┘ └───────────────┘
         │
┌────────▼────────┐
│   LRU Cache     │
│ (lru_cache.h)   │
│ O(1) get / put  │
│ 64-block cap.   │
└────────┬────────┘
         │
┌────────▼──────────────────────────────────────────────────────┐
│                  virtual_disk.bin  (4 MB binary file)         │
│  [SB][InBM][DtBM][──── Inode Table ────][── Data Blocks ──]  │
│   0    1     2      3 ──────────── 10    11 ───────── 991    │
│                                          [── Journal ──]      │
│                                           992 ─────── 1023   │
└───────────────────────────────────────────────────────────────┘
```

---

## 📁 Project Structure

```
file-system-recovery-tool/
│
├── 📄 fs_structures.h      ← Superblock, Inode, DirEntry (on-disk structs)
├── 📄 bitmap.h             ← Free-space bitmap (inode + data block tracking)
├── 📄 lru_cache.h          ← LRU Block Cache — O(1) via list + hash map
├── 📄 journal.h            ← Write-ahead journal (crash recovery engine)
├── 📄 virtual_disk.h       ← Block device abstraction (read/write/crash sim)
├── 📄 filesystem.h         ← FileSystem class interface
├── 📄 filesystem.cpp       ← Complete FS implementation (~500 lines)
├── 📄 main.cpp             ← Interactive CLI (REPL)
├── 📄 Makefile             ← Build script
│
└── 📂 fs_web/              ← Web Dashboard (no server needed)
    ├── 📄 index.html       ← Semantic HTML structure
    ├── 📄 style.css        ← Dark theme, animations, responsive layout
    └── 📄 app.js           ← JS simulation engine (mirrors C++ logic)
```

---

## ⚡ Quick Start

### Prerequisites
- g++ with C++17 support
- Any modern browser (for dashboard)

### Build & Run (CLI)

```bash
# Clone the repo
git clone https://github.com/Dhruva105/file-system-recovery-tool.git
cd file-system-recovery-tool

# Compile
g++ -std=c++17 -Wall -O2 -o fstool main.cpp filesystem.cpp

# Run
.\fstool.exe        # Windows
./fstool            # Linux / Mac
```

### Web Dashboard
```
Open fs_web/index.html in any browser
No installation · No server · Just open and use
```

---

## 💻 CLI Reference

```bash
fstool> create <filename>          # Allocate inode + data blocks
fstool> write  <filename> <text>   # Write content (journaled)
fstool> read   <filename>          # Read via LRU cache
fstool> delete <filename>          # Free inode + blocks
fstool> ls                         # List root directory
fstool> info                       # Show superblock stats
fstool> crash  <filename>          # Simulate disk crash 💥
fstool> fsck                       # Run consistency checker 🔍
fstool> recover                    # Replay journal log 📋
fstool> defrag                     # Defragment disk blocks ⟳
fstool> cache                      # LRU cache statistics ◑
fstool> quit                       # Unmount and exit
```

### Sample Session

```
fstool> create report.txt
[FS] Created file 'report.txt' → inode 1

fstool> write report.txt Hello World from VFS Lab!
[FS] Wrote 26 bytes to 'report.txt'

fstool> crash report.txt
[CRASH] Simulated crash: inode 1 corrupted!

fstool> fsck
[Pass 1] Scanning inode table...
  [ERROR] Inode 1 has invalid type byte — clearing
[Pass 2] Comparing bitmaps...
  [MISMATCH] Bitmap[1]: disk=1 computed=0 → fixing
── Summary ──
  Errors found : 3
  Errors fixed : 3
  FS state     : REPAIRED ✓

fstool> cache
  Hit rate : 91.0%
  Hits     : 162
  Misses   : 16
```

---

## 🌐 Web Dashboard

A fully interactive visual dashboard built in **vanilla HTML + CSS + JS** — no frameworks, no dependencies.

| Panel | What It Shows |
|-------|--------------|
| **Disk Block Map** | Live 1024-block grid — color-coded by state |
| **File Table** | Inode, filename, size, block count, status |
| **System Log** | Real-time operation logs with timestamps |
| **Journal Log** | BEGIN → DATA → COMMIT → CHECKPOINT records |
| **LRU Meter** | Animated hit rate bar with hits/misses counter |
| **Superblock Strip** | Magic, free blocks, inodes, FS state |

### Block Color Legend
```
■ Dark Blue  → System blocks (superblock, inode table, bitmaps)
■ Dark       → Free data blocks
■ Green      → Allocated file data
■ Red        → Corrupted blocks (after crash simulation)
■ Purple     → Journal reserved area
```

---

## 🧠 How It Works

### 1. On-Disk Layout
```
Block 0      → Superblock (magic=0xDEADBEEF, counts, FS state)
Block 1      → Inode Bitmap (1 bit per inode — 0=free, 1=used)
Block 2      → Data Bitmap  (1 bit per data block)
Blocks 3–10  → Inode Table  (128 inodes × metadata + 12 direct block pointers)
Blocks 11–991→ Data Blocks  (actual file content)
Blocks 992–1023 → Journal   (write-ahead log)
```

### 2. Write-Ahead Journaling (Recovery)
```
WITHOUT journal:    WITH journal (this project):
─────────────────   ──────────────────────────────
1. Update inode     1. Write JOURNAL_BEGIN
   ← crash here     2. Log all block writes
   = orphaned       3. Write JOURNAL_COMMIT  ← atomic
     blocks         4. Apply to real disk
                    5. Write JOURNAL_CHECKPOINT

On crash recovery → replay all COMMIT'd records → consistent state
```

### 3. LRU Cache (O(1) Performance)
```cpp
// std::list  → recency order (front = most recent)
// unordered_map → O(1) lookup: block_number → list iterator

GET: if found → splice to front → cache hit
PUT: insert at front → if over capacity → evict from back (LRU)
```

### 4. Defragmentation Algorithm
```
1. Scan all inodes → collect block lists in file order
2. Reset data bitmap to all zeros
3. Re-place every block sequentially from position 0
4. Update all inode block pointers
Result: zero fragmentation, maximum sequential access speed
```

### 5. fsck — Consistency Checker
```
Pass 1: Walk every inode
        → validate type field
        → validate block pointer ranges
        → rebuild reference bitmaps from scratch

Pass 2: Compare on-disk bitmaps vs computed bitmaps
        → fix every mismatch
        → recompute free counts in superblock
        → mark FS state = CLEAN
```

---

## 📊 Performance

| Metric | Value |
|--------|-------|
| Disk Size | 4 MB (1024 × 4KB blocks) |
| Max Files | 127 (128 inodes − 1 root) |
| Max File Size | 48 KB (12 direct + indirect blocks) |
| LRU Cache Hit Rate | ~91% (tested on demo workload) |
| Cache Capacity | 64 blocks (256 KB in memory) |
| Journal Size | 32 blocks (128 KB) |

---

## 🎓 Academic Context

Built as part of **Operating Systems** coursework covering:
- File system internals (ext2/ext4 architecture)
- Crash consistency and journaling
- Free-space management algorithms  
- Disk I/O optimization techniques
- Data structure design (LRU, bitmaps, inodes)

---

## 👨‍💻 Author

<div align="center">

<h3>Dhruv Pal</h3>

<p>
B.Tech Computer Science & Engineering (AI & ML)<br/>
Aspiring Software Engineer • Machine Learning Enthusiast
</p>

<a href="https://github.com/Dhruva105">
  <img src="https://img.shields.io/badge/-GitHub-181717?style=for-the-badge&logo=github&logoColor=white" />
</a>


<a href="https://www.linkedin.com/in/dhruv-pal-521120311/">
  <img src="https://img.shields.io/badge/-LinkedIn-0A66C2?style=for-the-badge&logo=linkedin&logoColor=white" />
</a>
</div>

---

<div align="center">

![Footer](https://capsule-render.vercel.app/api?type=waving&color=4f8cff&height=100&section=footer)

⭐ **Star this repo if you found it helpful!**

</div>
