# ============================================================
#  Makefile  –  File System Recovery & Optimization Tool
# ============================================================
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
TARGET   := fstool
SRCS     := main.cpp filesystem.cpp
HEADERS  := fs_structures.h bitmap.h lru_cache.h journal.h \
            virtual_disk.h filesystem.h

.PHONY: all clean demo

all: $(TARGET)

$(TARGET): $(SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)
	@echo "Build successful → ./$(TARGET)"

clean:
	rm -f $(TARGET) virtual_disk.bin

# ── Quick demo sequence ───────────────────────────────────────
demo: $(TARGET)
	@echo "=== Running automated demo ==="
	printf 'create readme.txt\nwrite readme.txt Hello World from FS Tool!\nread readme.txt\nls\ninfo\ncrash readme.txt\nfsck\ndefrag\ncache\nquit\n' | ./$(TARGET)