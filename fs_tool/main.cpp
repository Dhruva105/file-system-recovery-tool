// ============================================================
//  main.cpp  –  Interactive CLI for the FS Recovery & Opt Tool
//
//  Usage: ./fstool [disk_path]
//  Default disk file: virtual_disk.bin
// ============================================================
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include "filesystem.h"

// ── Helper: split line into tokens ───────────────────────────
static std::vector<std::string> tokenize(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> token) tokens.push_back(token);
    return tokens;
}

// ── Pretty banner ─────────────────────────────────────────────
static void print_banner() {
    std::cout << R"(
╔══════════════════════════════════════════════════════════╗
║     File System Recovery & Optimization Tool v1.0       ║
║     Unix-like VFS | Journaling | fsck | LRU | Defrag    ║
╚══════════════════════════════════════════════════════════╝
Type 'help' for command list.
)" << std::endl;
}

static void print_help() {
    std::cout << R"(
┌─────────────────────────────────────────────────────────┐
│  File Operations                                        │
│  create <name>          Create a new empty file         │
│  write  <name> <text>   Write text content to file      │
│  read   <name>          Read and print file content     │
│  delete <name>          Delete a file                   │
│  ls                     List all files                  │
│                                                         │
│  Recovery & Repair                                      │
│  crash  <name>          Simulate crash (corrupt inode)  │
│  fsck                   Run consistency checker         │
│  recover                Replay journal to recover data  │
│                                                         │
│  Optimization                                           │
│  defrag                 Defragment disk blocks          │
│  cache                  Show LRU cache statistics       │
│                                                         │
│  System                                                 │
│  info                   Show superblock info            │
│  format                 Wipe and re-format disk         │
│  help                   Show this menu                  │
│  quit / exit            Exit the tool                   │
└─────────────────────────────────────────────────────────┘
)" << std::endl;
}

// ─────────────────────────────────────────────────────────────
//  Main REPL
// ─────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    std::string disk_path = (argc > 1) ? argv[1] : "virtual_disk.bin";
    print_banner();

    FileSystem fs(disk_path);
    std::cout << "[FS] Mounted disk: " << disk_path << "\n\n";

    std::string line;
    while (true) {
        std::cout << "fstool> ";
        if (!std::getline(std::cin, line)) break;

        // Strip trailing whitespace
        while (!line.empty() && std::isspace(line.back())) line.pop_back();
        if (line.empty()) continue;

        auto tokens = tokenize(line);
        if (tokens.empty()) continue;

        const std::string& cmd = tokens[0];

        try {
            // ── ls ───────────────────────────────────────────
            if (cmd == "ls") {
                auto files = fs.list_files();
                if (files.empty()) {
                    std::cout << "  (empty directory)\n";
                } else {
                    std::cout << "  Files (" << files.size() << "):\n";
                    for (auto& f : files) std::cout << "    " << f << "\n";
                }
            }
            // ── create ───────────────────────────────────────
            else if (cmd == "create") {
                if (tokens.size() < 2) { std::cout << "Usage: create <name>\n"; continue; }
                fs.create_file(tokens[1]);
            }
            // ── write ────────────────────────────────────────
            else if (cmd == "write") {
                if (tokens.size() < 3) { std::cout << "Usage: write <name> <text...>\n"; continue; }
                // Re-join text after filename
                std::string content;
                for (size_t i = 2; i < tokens.size(); ++i) {
                    if (i > 2) content += ' ';
                    content += tokens[i];
                }
                std::vector<uint8_t> data(content.begin(), content.end());
                fs.write_file(tokens[1], data);
            }
            // ── read ─────────────────────────────────────────
            else if (cmd == "read") {
                if (tokens.size() < 2) { std::cout << "Usage: read <name>\n"; continue; }
                auto data = fs.read_file(tokens[1]);
                std::cout << "  Content (" << data.size() << " bytes):\n  ";
                for (uint8_t b : data) {
                    if (b == '\0') break;
                    std::cout << static_cast<char>(b);
                }
                std::cout << "\n";
            }
            // ── delete ───────────────────────────────────────
            else if (cmd == "delete" || cmd == "rm") {
                if (tokens.size() < 2) { std::cout << "Usage: delete <name>\n"; continue; }
                fs.delete_file(tokens[1]);
            }
            // ── crash ────────────────────────────────────────
            else if (cmd == "crash") {
                if (tokens.size() < 2) { std::cout << "Usage: crash <name>\n"; continue; }
                fs.simulate_crash(tokens[1]);
                std::cout << "  TIP: Run 'fsck' or 'recover' to repair.\n";
            }
            // ── fsck ─────────────────────────────────────────
            else if (cmd == "fsck") {
                std::cout << fs.fsck();
            }
            // ── recover ──────────────────────────────────────
            else if (cmd == "recover") {
                std::cout << fs.recover_from_journal();
            }
            // ── defrag ───────────────────────────────────────
            else if (cmd == "defrag") {
                std::cout << fs.defragment();
            }
            // ── cache ────────────────────────────────────────
            else if (cmd == "cache") {
                fs.print_cache_stats();
            }
            // ── info ─────────────────────────────────────────
            else if (cmd == "info") {
                fs.print_superblock();
            }
            // ── format ───────────────────────────────────────
            else if (cmd == "format") {
                std::cout << "  WARNING: This will erase all data. Confirm? [y/N]: ";
                std::string confirm;
                std::getline(std::cin, confirm);
                if (confirm == "y" || confirm == "Y") fs.format();
                else std::cout << "  Aborted.\n";
            }
            // ── help ─────────────────────────────────────────
            else if (cmd == "help") {
                print_help();
            }
            // ── quit ─────────────────────────────────────────
            else if (cmd == "quit" || cmd == "exit") {
                std::cout << "[FS] Unmounting. Goodbye.\n";
                break;
            }
            else {
                std::cout << "  Unknown command: '" << cmd << "'. Type 'help' for list.\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "  [ERROR] " << e.what() << "\n";
        }
    }
    return 0;
}
