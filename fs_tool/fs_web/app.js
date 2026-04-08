// ============================================================
//  app.js — VFS Lab Simulation Engine
//  Mirrors the exact logic of the C++ backend in JavaScript
//  for visual demonstration purposes.
// ============================================================

const App = (() => {

  // ── Constants (mirror C++ values) ──────────────────────────
  const TOTAL_BLOCKS      = 1024;
  const BLOCK_SIZE        = 4096;
  const INODE_COUNT       = 128;
  const DIRECT_BLOCKS     = 12;
  const DATA_START_BLOCK  = 11;   // blocks 0-10 = system
  const JOURNAL_START     = 992;  // last 32 blocks
  const DATA_BLOCK_COUNT  = JOURNAL_START - DATA_START_BLOCK; // 981

  // ── In-memory state ────────────────────────────────────────
  let state = {
    files: [],         // { name, inodeId, size, blockNums, status, createdAt }
    nextInode: 1,
    inodeBitmap: new Array(INODE_COUNT).fill(false),
    dataBitmap:  new Array(DATA_BLOCK_COUNT).fill(false),
    fsState: 'CLEAN',  // CLEAN | DIRTY | REPAIRING
    cacheHits: 0,
    cacheMisses: 0,
    cacheHistory: [],  // last N block accesses
    journalRecords: [],
    nextTxnId: 1,
    totalBytesWritten: 0,
  };

  // ── LRU Cache simulation (capacity = 64 blocks) ─────────────
  const CACHE_CAPACITY = 64;
  let lruCache = new Map(); // blockNum → last access time (LRU via Map insertion order)

  function cacheGet(blockNum) {
    if (lruCache.has(blockNum)) {
      // Move to front (most recent)
      const val = lruCache.get(blockNum);
      lruCache.delete(blockNum);
      lruCache.set(blockNum, val);
      state.cacheHits++;
      return true;
    }
    state.cacheMisses++;
    cachePut(blockNum);
    return false;
  }

  function cachePut(blockNum) {
    if (lruCache.size >= CACHE_CAPACITY) {
      const oldest = lruCache.keys().next().value;
      lruCache.delete(oldest);
    }
    lruCache.set(blockNum, Date.now());
  }

  // ── Block allocation ────────────────────────────────────────
  function allocDataBlock() {
    for (let i = 0; i < DATA_BLOCK_COUNT; i++) {
      if (!state.dataBitmap[i]) {
        state.dataBitmap[i] = true;
        return DATA_START_BLOCK + i;
      }
    }
    return -1; // disk full
  }

  function freeDataBlock(blockNum) {
    const idx = blockNum - DATA_START_BLOCK;
    if (idx >= 0 && idx < DATA_BLOCK_COUNT) {
      state.dataBitmap[idx] = false;
    }
  }

  function allocInode() {
    for (let i = 0; i < INODE_COUNT; i++) {
      if (!state.inodeBitmap[i]) {
        state.inodeBitmap[i] = true;
        return i;
      }
    }
    return -1;
  }

  function freeInode(id) {
    state.inodeBitmap[id] = false;
  }

  // ── Journal helpers ─────────────────────────────────────────
  function journalBegin() {
    const txn = state.nextTxnId++;
    state.journalRecords.push({ type: 'BEGIN', txn, detail: 'Transaction started' });
    renderJournal();
    return txn;
  }

  function journalLog(txn, blockNum, detail) {
    cacheGet(blockNum); // simulate cache access
    state.journalRecords.push({ type: 'DATA', txn, detail: `Block ${blockNum} — ${detail}` });
    renderJournal();
  }

  function journalCommit(txn) {
    state.journalRecords.push({ type: 'COMMIT', txn, detail: 'All changes logged, safe to apply' });
    renderJournal();
  }

  function journalCheckpoint(txn) {
    state.journalRecords.push({ type: 'CHECKPOINT', txn, detail: 'Changes applied to disk' });
    renderJournal();
  }

  // ── Helpers ─────────────────────────────────────────────────
  function getFreeBlocks() {
    return state.dataBitmap.filter(b => !b).length;
  }

  function getFreeInodes() {
    return state.inodeBitmap.filter(b => !b).length;
  }

  function blocksNeeded(sizeBytes) {
    return Math.max(1, Math.ceil(sizeBytes / BLOCK_SIZE));
  }

  function formatBytes(n) {
    if (n < 1024) return n + ' B';
    if (n < 1024*1024) return (n/1024).toFixed(1) + ' KB';
    return (n/(1024*1024)).toFixed(1) + ' MB';
  }

  function now() {
    return new Date().toLocaleTimeString('en-GB', { hour:'2-digit', minute:'2-digit', second:'2-digit' });
  }

  function shortTime() {
    const d = new Date();
    return d.getHours().toString().padStart(2,'0') + ':' +
           d.getMinutes().toString().padStart(2,'0');
  }

  // ── Core Operations ─────────────────────────────────────────

  function createFile() {
    const name = document.getElementById('fname').value.trim();
    if (!name) return toast('Enter a filename', 'error');
    if (name.length > 27) return toast('Filename too long (max 27 chars)', 'error');
    if (state.files.find(f => f.name === name)) return toast(`'${name}' already exists`, 'error');

    const content = document.getElementById('fcontent').value;
    const size = content.length || 0;
    const needed = blocksNeeded(Math.max(size, 1));

    if (getFreeBlocks() < needed) return toast('Disk full!', 'error');
    if (getFreeInodes() < 1) return toast('No free inodes!', 'error');

    const txn = journalBegin();
    const inodeId = allocInode();
    const blocks = [];

    for (let i = 0; i < needed; i++) {
      const blk = allocDataBlock();
      if (blk === -1) { toast('Disk full during write!', 'error'); return; }
      blocks.push(blk);
      journalLog(txn, blk, `data block for '${name}'`);
    }

    const file = {
      name, inodeId,
      size: Math.max(size, 0),
      blockNums: blocks,
      content,
      status: 'OK',
      createdAt: now(),
    };
    state.files.push(file);
    journalCommit(txn);
    journalCheckpoint(txn);

    state.totalBytesWritten += size;
    log(`Created file '${name}' → inode ${inodeId}, ${needed} block(s) allocated`, 'success');
    toast(`File '${name}' created`, 'success');
    updateAll();
    animateBlocks(blocks, 'blk-new');
    document.getElementById('fname').value = '';
    document.getElementById('fcontent').value = '';
  }

  function writeFile() {
    const name = document.getElementById('fname').value.trim();
    const content = document.getElementById('fcontent').value;
    if (!name) return toast('Enter a filename', 'error');
    const file = state.files.find(f => f.name === name);
    if (!file) return toast(`File '${name}' not found`, 'error');
    if (file.status === 'CORRUPT') return toast('Cannot write to corrupted file — run fsck first', 'error');

    const txn = journalBegin();
    // Free old blocks
    file.blockNums.forEach(b => freeDataBlock(b));
    file.blockNums = [];

    const needed = blocksNeeded(Math.max(content.length, 1));
    for (let i = 0; i < needed; i++) {
      const blk = allocDataBlock();
      if (blk === -1) { toast('Disk full!', 'error'); return; }
      file.blockNums.push(blk);
      journalLog(txn, blk, `write block for '${name}'`);
    }

    file.content = content;
    file.size = content.length;
    journalCommit(txn);
    journalCheckpoint(txn);
    state.totalBytesWritten += content.length;

    log(`Wrote ${content.length} bytes to '${name}'`, 'success');
    toast(`Wrote ${content.length} B to '${name}'`, 'success');
    updateAll();
    animateBlocks(file.blockNums, 'blk-new');
    document.getElementById('fcontent').value = '';
  }

  function readFile() {
    const name = document.getElementById('fname').value.trim() ||
                 document.getElementById('fname').placeholder;
    const actualName = document.getElementById('fname').value.trim();
    if (!actualName) return toast('Enter a filename to read', 'error');

    const file = state.files.find(f => f.name === actualName);
    if (!file) return toast(`File '${actualName}' not found`, 'error');
    if (file.status === 'CORRUPT') return toast('File is corrupted — run fsck', 'error');

    // Simulate cache access for each block
    file.blockNums.forEach(b => cacheGet(b));

    const panel = document.getElementById('readPanel');
    const body  = document.getElementById('readBody');
    panel.style.display = 'block';
    body.textContent = file.content || '(empty file)';

    log(`Read '${file.name}' — ${file.size} bytes, ${file.blockNums.length} block(s) accessed`, 'info');
    updateCacheDisplay();
  }

  function deleteFile() {
    const name = document.getElementById('fname').value.trim();
    if (!name) return toast('Enter a filename to delete', 'error');
    const idx = state.files.findIndex(f => f.name === name);
    if (idx === -1) return toast(`File '${name}' not found`, 'error');

    const file = state.files[idx];
    const txn = journalBegin();
    file.blockNums.forEach(b => freeDataBlock(b));
    freeInode(file.inodeId);
    state.files.splice(idx, 1);
    journalCommit(txn);
    journalCheckpoint(txn);

    log(`Deleted '${name}' — inode ${file.inodeId} freed, ${file.blockNums.length} block(s) released`, 'warn');
    toast(`'${name}' deleted`, 'success');
    updateAll();
    document.getElementById('fname').value = '';
  }

  // ── Recovery ────────────────────────────────────────────────

  function simulateCrash() {
    if (state.files.length === 0) return toast('Create some files first', 'error');

    setFsState('DIRTY');
    const name = document.getElementById('fname').value.trim();
    const target = name
      ? state.files.find(f => f.name === name)
      : state.files[Math.floor(Math.random() * state.files.length)];

    if (!target) return toast(`File '${name}' not found`, 'error');

    target.status = 'CORRUPT';

    // Corrupt ~30% of its blocks (mark as corrupt in bitmap sense)
    const corruptCount = Math.max(1, Math.floor(target.blockNums.length * 0.4));
    target.corruptBlocks = target.blockNums.slice(0, corruptCount);

    log(`💥 CRASH SIMULATED — inode ${target.inodeId} corrupted for '${target.name}'`, 'crash');
    log(`   ${corruptCount} block(s) marked as corrupt. Metadata inconsistent.`, 'crash');
    toast(`Crash! '${target.name}' corrupted`, 'error');

    // Add journal entry showing the incomplete transaction
    state.journalRecords.push({ type: 'BEGIN', txn: state.nextTxnId++, detail: '⚠ Incomplete — crash before COMMIT' });
    renderJournal();

    updateAll();
    document.getElementById('panel-recovery').classList.add('shake');
    setTimeout(() => document.getElementById('panel-recovery').classList.remove('shake'), 500);
  }

  function runFsck() {
    setFsState('REPAIRING');
    log('🔍 fsck started — Pass 1: Scanning inode table...', 'warn');

    let errFound = 0, errFixed = 0;
    const corruptFiles = state.files.filter(f => f.status === 'CORRUPT');

    setTimeout(() => {
      corruptFiles.forEach(f => {
        log(`   [ERROR] Inode ${f.inodeId} has invalid type — clearing corrupt state`, 'warn');
        errFound++;
      });

      setTimeout(() => {
        log('🔍 fsck — Pass 2: Comparing bitmaps...', 'warn');
        setTimeout(() => {
          corruptFiles.forEach(f => {
            log(`   [MISMATCH] Bitmap fix for inode ${f.inodeId} → repaired`, 'repair');
            f.status = 'REPAIRED';
            f.corruptBlocks = [];
            errFixed++;
          });

          log(`✓ fsck complete — ${errFound} errors found, ${errFixed} fixed. FS state: CLEAN`, 'success');
          toast('fsck complete — filesystem repaired', 'success');
          setFsState('CLEAN');
          updateAll();
          document.getElementById('panel-recovery').classList.add('panel-highlight');
          setTimeout(() => document.getElementById('panel-recovery').classList.remove('panel-highlight'), 1000);
        }, 600);
      }, 700);
    }, 500);
  }

  function recoverJournal() {
    const uncommitted = state.journalRecords.filter(r =>
      r.type === 'BEGIN' && r.detail.includes('⚠')
    );

    if (uncommitted.length === 0 && state.fsState !== 'DIRTY') {
      log('Journal: Nothing to recover — no uncommitted transactions.', 'info');
      toast('No journal recovery needed', 'success');
      return;
    }

    log('📋 Replaying journal — restoring committed blocks...', 'warn');
    const committed = state.journalRecords.filter(r => r.type === 'COMMIT');
    committed.forEach(r => {
      state.journalRecords.push({ type: 'REPLAY', txn: r.txn, detail: `Replayed txn #${r.txn}` });
    });

    state.files.filter(f => f.status === 'CORRUPT').forEach(f => {
      f.status = 'OK';
      f.corruptBlocks = [];
    });

    log('✓ Journal recovery complete. FS is clean.', 'success');
    toast('Journal replayed successfully', 'success');
    setFsState('CLEAN');
    renderJournal();
    updateAll();
  }

  // ── Optimization ────────────────────────────────────────────

  function defragment() {
    if (state.files.length === 0) return toast('No files to defragment', 'error');

    log('⟳ Defragmentation started — compacting blocks...', 'warn');

    // Reset data bitmap and repack
    state.dataBitmap.fill(false);
    let nextFree = DATA_START_BLOCK;
    let moved = 0;

    state.files.forEach(f => {
      const newBlocks = [];
      f.blockNums.forEach(oldBlk => {
        const newBlk = nextFree++;
        state.dataBitmap[newBlk - DATA_START_BLOCK] = true;
        if (oldBlk !== newBlk) {
          moved++;
          // Invalidate old cache entry
          lruCache.delete(oldBlk);
        }
        newBlocks.push(newBlk);
      });
      f.blockNums = newBlocks;
    });

    log(`✓ Defrag complete — ${moved} block(s) relocated. All files contiguous.`, 'success');
    toast(`Defrag done — ${moved} blocks moved`, 'success');
    updateAll();
    renderDiskGrid('defrag');
    setTimeout(() => renderDiskGrid(), 800);
  }

  function showCacheStats() {
    const hits   = state.cacheHits;
    const misses = state.cacheMisses;
    const total  = hits + misses;
    const rate   = total ? ((hits / total) * 100).toFixed(1) : 0;

    const meter = document.getElementById('cacheMeter');
    meter.style.display = 'block';
    document.getElementById('cacheHitRate').textContent = rate + '%';
    document.getElementById('cacheHits').textContent    = hits;
    document.getElementById('cacheMisses').textContent  = misses;
    setTimeout(() => {
      document.getElementById('cacheBar').style.width = rate + '%';
    }, 50);

    log(`LRU Cache — Hits: ${hits}, Misses: ${misses}, Hit Rate: ${rate}%`, 'info');
    toast(`Cache hit rate: ${rate}%`, 'success');
  }

  function format() {
    if (!confirm('Format will erase all files. Continue?')) return;
    state.files = [];
    state.nextInode = 1;
    state.inodeBitmap.fill(false);
    state.dataBitmap.fill(false);
    state.fsState = 'CLEAN';
    state.cacheHits = 0;
    state.cacheMisses = 0;
    state.journalRecords = [];
    state.nextTxnId = 1;
    state.totalBytesWritten = 0;
    lruCache.clear();

    log('⬚ Disk formatted. All data erased. FS initialized.', 'warn');
    toast('Disk formatted', 'success');
    document.getElementById('cacheMeter').style.display = 'none';
    document.getElementById('readPanel').style.display  = 'none';
    updateAll();
    clearJournal();
  }

  function clearLog() {
    document.getElementById('logBody').innerHTML = '';
    log('Log cleared.', 'info');
  }

  // ── Rendering ───────────────────────────────────────────────

  function renderDiskGrid(mode) {
    const grid = document.getElementById('diskGrid');
    grid.innerHTML = '';
    const tooltip = document.getElementById('diskTooltip');

    for (let i = 0; i < TOTAL_BLOCKS; i++) {
      const blk = document.createElement('div');
      blk.className = 'blk';

      let cls = 'blk-free';
      let label = `Block ${i} — Free`;

      if (i < DATA_START_BLOCK) {
        cls = 'blk-sys';
        const sysLabels = ['Superblock','Inode BM','Data BM','Inode Tbl','Inode Tbl','Inode Tbl','Inode Tbl','Inode Tbl','Inode Tbl','Inode Tbl','Inode Tbl'];
        label = `Block ${i} — ${sysLabels[i] || 'System'}`;
      } else if (i >= JOURNAL_START) {
        cls = 'blk-journal';
        label = `Block ${i} — Journal`;
      } else {
        const dataIdx = i - DATA_START_BLOCK;
        if (state.dataBitmap[dataIdx]) {
          // Find which file owns this block
          const owner = state.files.find(f => f.blockNums.includes(i));
          if (owner && owner.status === 'CORRUPT' && owner.corruptBlocks && owner.corruptBlocks.includes(i)) {
            cls = 'blk-corrupt';
            label = `Block ${i} — CORRUPT (${owner.name})`;
          } else if (mode === 'defrag') {
            cls = 'blk-defrag';
            label = `Block ${i} — Defragmenting`;
          } else {
            cls = 'blk-used';
            label = `Block ${i} — ${owner ? `'${owner.name}'` : 'Used'}`;
          }
        }
      }

      blk.classList.add(cls);
      blk.addEventListener('mouseenter', e => {
        tooltip.textContent = label;
        tooltip.style.display = 'block';
      });
      blk.addEventListener('mousemove', e => {
        tooltip.style.left = (e.clientX + 12) + 'px';
        tooltip.style.top  = (e.clientY + 12) + 'px';
      });
      blk.addEventListener('mouseleave', () => {
        tooltip.style.display = 'none';
      });

      grid.appendChild(blk);
    }
  }

  function animateBlocks(blockNums, animClass) {
    const grid = document.getElementById('diskGrid');
    const cells = grid.children;
    blockNums.forEach(b => {
      if (cells[b]) {
        cells[b].classList.add(animClass);
        setTimeout(() => cells[b].classList.remove(animClass), 500);
      }
    });
  }

  function renderFileTable() {
    const tbody = document.getElementById('fileTableBody');
    const badge = document.getElementById('fileCountBadge');
    badge.textContent = `${state.files.length} file${state.files.length !== 1 ? 's' : ''}`;

    if (state.files.length === 0) {
      tbody.innerHTML = `<tr class="empty-row"><td colspan="6">No files yet. Create one to begin.</td></tr>`;
      return;
    }

    tbody.innerHTML = state.files.map(f => {
      const pillClass = f.status === 'CORRUPT' ? 'pill-corrupt' :
                        f.status === 'REPAIRED' ? 'pill-repaired' : 'pill-ok';
      const pillDot   = f.status === 'CORRUPT' ? '⚠' :
                        f.status === 'REPAIRED' ? '↑' : '●';
      return `
        <tr onclick="App.quickSelect('${f.name}')">
          <td class="inode-cell">#${f.inodeId}</td>
          <td class="fname-cell">${f.name}</td>
          <td class="size-cell">${formatBytes(f.size)}</td>
          <td>${f.blockNums.length} blk${f.blockNums.length !== 1 ? 's' : ''}</td>
          <td><span class="status-pill ${pillClass}">${pillDot} ${f.status}</span></td>
          <td style="color:var(--text3);font-size:11px">${f.createdAt}</td>
        </tr>`;
    }).join('');
  }

  function renderJournal() {
    const body = document.getElementById('journalBody');
    if (state.journalRecords.length === 0) {
      body.innerHTML = '<div class="journal-empty">No journal entries yet.</div>';
      return;
    }
    const last20 = state.journalRecords.slice(-20).reverse();
    body.innerHTML = last20.map(r => {
      const typeMap = {
        BEGIN: 'jt-begin', DATA: 'jt-data', COMMIT: 'jt-commit',
        CHECKPOINT: 'jt-checkpoint', REPLAY: 'jt-replay'
      };
      return `
        <div class="journal-record">
          <span class="jrec-type ${typeMap[r.type] || ''}">${r.type}</span>
          <span class="jrec-detail">${r.detail}</span>
          <span class="jrec-txn">txn#${r.txn}</span>
        </div>`;
    }).join('');
  }

  function clearJournal() {
    document.getElementById('journalBody').innerHTML =
      '<div class="journal-empty">No journal entries yet.</div>';
  }

  function updateSuperblock() {
    const free = getFreeBlocks();
    const freeInodes = getFreeInodes();
    const usedKB = ((DATA_BLOCK_COUNT - free) * BLOCK_SIZE / 1024).toFixed(0);

    document.getElementById('sbFree').textContent   = free;
    document.getElementById('sbInodes').textContent = freeInodes;
    document.getElementById('sbState').textContent  = state.fsState;
    document.getElementById('sbState').style.color  =
      state.fsState === 'CLEAN' ? 'var(--success)' :
      state.fsState === 'DIRTY' ? 'var(--danger)' : 'var(--warn)';

    document.getElementById('navFree').textContent  = formatBytes(free * BLOCK_SIZE);
    document.getElementById('navFiles').textContent = state.files.length;
  }

  function updateCacheDisplay() {
    const total = state.cacheHits + state.cacheMisses;
    const rate  = total ? ((state.cacheHits / total) * 100).toFixed(1) : 0;
    if (document.getElementById('cacheMeter').style.display !== 'none') {
      document.getElementById('cacheHitRate').textContent = rate + '%';
      document.getElementById('cacheHits').textContent    = state.cacheHits;
      document.getElementById('cacheMisses').textContent  = state.cacheMisses;
      document.getElementById('cacheBar').style.width     = rate + '%';
    }
  }

  function setFsState(s) {
    state.fsState = s;
    const badge = document.getElementById('fsBadge');
    const dot   = document.getElementById('statusDot');
    const txt   = document.getElementById('statusText');
    badge.className = 'fs-status-badge ' + s.toLowerCase();
    txt.textContent = s;
  }

  function updateAll() {
    renderDiskGrid();
    renderFileTable();
    updateSuperblock();
    updateCacheDisplay();
  }

  // ── Utility ─────────────────────────────────────────────────

  function log(msg, type = 'info') {
    const body = document.getElementById('logBody');
    const entry = document.createElement('div');
    entry.className = `log-entry log-${type}`;
    entry.innerHTML = `<span class="log-time">${shortTime()}</span><span class="log-msg">${msg}</span>`;
    body.appendChild(entry);
    body.scrollTop = body.scrollHeight;
  }

  let toastTimer;
  function toast(msg, type = 'info') {
    const el = document.getElementById('toast');
    el.textContent = msg;
    el.className = `toast show toast-${type}`;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => el.classList.remove('show'), 2800);
  }

  function quickSelect(name) {
    document.getElementById('fname').value = name;
  }

  // ── Init ─────────────────────────────────────────────────────
  function init() {
    // Mark system blocks in bitmap conceptually
    // (they're always "used" — just rendered separately)
    renderDiskGrid();
    updateSuperblock();
    renderJournal();

    // Keyboard shortcut: Enter in fname/content triggers create
    document.getElementById('fname').addEventListener('keydown', e => {
      if (e.key === 'Enter') createFile();
    });

    log('VFS Lab v1.0 — Virtual disk mounted (4 MB, 1024 blocks)', 'info');
    log('Superblock OK — Magic: 0xDEADBEEF — FS State: CLEAN', 'success');
    log('LRU Cache initialized (capacity: 64 blocks)', 'info');
    log('Journal initialized — write-ahead logging active', 'info');
  }

  document.addEventListener('DOMContentLoaded', init);

  // ── Public API ───────────────────────────────────────────────
  return {
    createFile, writeFile, readFile, deleteFile,
    simulateCrash, runFsck, recoverJournal,
    defragment, showCacheStats,
    format, clearLog, quickSelect,
  };

})();
