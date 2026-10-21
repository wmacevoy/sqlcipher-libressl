/*
** walseal_test.c -- walseal's checks, natively, against this repository's
** amalgamation and LibreSSL (REQ8/DUR-2, REQ6/ENC-3).  Each command exits
** non-zero when its check fails.
**
** Provenance: sqlcipher-embedded, branch experiment/wal-seal, commit
** 5d50c676, embedded/test/walseal_test.c.  Changed here: io and leak assert
** what they used to only print (sealed I/O identical to stock; no WAL
** header field readable without the key).
**
**   walseal_test io    <dir> [rows] [txn] [ckpt]   I/O shape: sealed == stock
**   walseal_test leak  <dir>                       what a WAL snapshot reveals
**   walseal_test crash <dir> <mode> [seed]         cut/tear/reorder replay;
**                                                  mode: stock|sealed
**
** Stack: SQLCipher -> [walseal] -> count -> unix.  "count" measures what
** reaches the filesystem (calls, 4 KB alignment, blocks touched, and the
** same after puck-style write combining) and can record every write,
** truncate and sync for crash replay.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "sqlite3.h"
#include "walseal.h"

typedef sqlite3_int64 i64;
#define BLK 4096
static const char *KEYHEX = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
/* Once known, the salt goes in the key (x'key||salt'): SQLCipher then need
** not read it from page 1, which a torn checkpoint write can destroy. */
static char salthex[33];
static const unsigned char WALKEY[32] = "walseal test key, 32 bytes long";

/* ---- count VFS ------------------------------------------------------------ */

enum { F_MAIN, F_WAL, F_OTHER };
typedef struct Stat {
  i64 nW, bW, nWun, blkW, partW;      /* per call */
  i64 runs, runPart;                  /* after write combining */
  i64 nR, bR, nRun, blkR, nS;
} Stat;
static Stat st[3];
typedef struct Op { int fid; char kind; i64 off; int n; unsigned char *d; } Op;
static Op *ops; static int nOps, aOps, recording;
static void rec(int fid, char kind, i64 off, int n, const void *d){
  if(!recording || fid == F_OTHER) return;
  if(nOps == aOps){ aOps = aOps ? 2*aOps : 1024; ops = realloc(ops, aOps * sizeof(Op)); }
  ops[nOps].fid = fid; ops[nOps].kind = kind; ops[nOps].off = off; ops[nOps].n = n;
  ops[nOps].d = d ? memcpy(malloc(n), d, n) : 0;
  nOps++;
}

typedef struct CFile { sqlite3_file base; sqlite3_file *real; int fid; i64 runOff, runLen; } CFile;
static sqlite3_vfs *cUnder;
#define CR(f) (((CFile*)(f))->real)
#define CM(f) (CR(f)->pMethods)

static i64 blocks(i64 off, i64 n){ return n ? (off + n - 1) / BLK - off / BLK + 1 : 0; }
static i64 partial(i64 off, i64 n){
  i64 e = off + n, k = 0;
  if(!n) return 0;
  if(off % BLK) k++;
  if(e % BLK && (blocks(off, n) > 1 || !(off % BLK))) k++;
  return k;
}
static void endRun(CFile *p){
  if(p->runLen){ st[p->fid].runs++; st[p->fid].runPart += partial(p->runOff, p->runLen); }
  p->runLen = 0;
}
static int cClose(sqlite3_file *f){ int rc; endRun((CFile*)f); rc = CM(f)->xClose(CR(f)); return rc; }
static int cRead(sqlite3_file *f, void *z, int n, i64 o){
  Stat *s = &st[((CFile*)f)->fid];
  endRun((CFile*)f);
  s->nR++; s->bR += n; s->nRun += (o % BLK || n % BLK) != 0; s->blkR += blocks(o, n);
  return CM(f)->xRead(CR(f), z, n, o);
}
static int cWrite(sqlite3_file *f, const void *z, int n, i64 o){
  CFile *p = (CFile*)f;
  Stat *s = &st[p->fid];
  s->nW++; s->bW += n; s->nWun += (o % BLK || n % BLK) != 0; s->blkW += blocks(o, n); s->partW += partial(o, n);
  if(p->runLen && o == p->runOff + p->runLen) p->runLen += n;
  else { endRun(p); p->runOff = o; p->runLen = n; }
  rec(p->fid, 'W', o, n, z);
  return CM(f)->xWrite(CR(f), z, n, o);
}
static int cTruncate(sqlite3_file *f, i64 sz){ endRun((CFile*)f); rec(((CFile*)f)->fid, 'T', sz, 0, 0); return CM(f)->xTruncate(CR(f), sz); }
static int cSync(sqlite3_file *f, int fl){
  CFile *p = (CFile*)f;
  endRun(p); st[p->fid].nS++; rec(p->fid, 'S', 0, 0, 0);
  return CM(f)->xSync(CR(f), fl);
}
static int cFileSize(sqlite3_file *f, i64 *s){ return CM(f)->xFileSize(CR(f), s); }
static int cLock(sqlite3_file *f, int l){ return CM(f)->xLock(CR(f), l); }
static int cUnlock(sqlite3_file *f, int l){ return CM(f)->xUnlock(CR(f), l); }
static int cCheck(sqlite3_file *f, int *r){ return CM(f)->xCheckReservedLock(CR(f), r); }
static int cFcntl(sqlite3_file *f, int op, void *a){ return CM(f)->xFileControl(CR(f), op, a); }
static int cSector(sqlite3_file *f){ (void)f; return BLK; }
static int cDevice(sqlite3_file *f){ (void)f; return 0; }   /* as the puck VFS: no PSOW */
static const sqlite3_io_methods cMethods = {
  1, cClose, cRead, cWrite, cTruncate, cSync, cFileSize, cLock, cUnlock, cCheck, cFcntl,
  cSector, cDevice, 0, 0, 0, 0, 0, 0
};
static int cOpen(sqlite3_vfs *v, const char *z, sqlite3_file *f, int flags, int *out){
  CFile *p = (CFile*)f;
  int rc;
  (void)v;
  memset(p, 0, sizeof(*p));
  p->real = (sqlite3_file*)&p[1];
  p->fid = flags & SQLITE_OPEN_MAIN_DB ? F_MAIN : flags & SQLITE_OPEN_WAL ? F_WAL : F_OTHER;
  rc = cUnder->xOpen(cUnder, z, p->real, flags, out);
  if(rc){ p->real->pMethods = 0; return rc; }
  p->base.pMethods = &cMethods;
  return SQLITE_OK;
}
static sqlite3_vfs cVfs;
static void count_register(void){
  cUnder = sqlite3_vfs_find(0);
  cVfs = *cUnder;
  cVfs.zName = "count";
  cVfs.szOsFile = (int)sizeof(CFile) + cUnder->szOsFile;
  cVfs.xOpen = cOpen;
  cVfs.pNext = 0;
  sqlite3_vfs_register(&cVfs, 0);
}

/* ---- database ---------------------------------------------------------- */

static int ok(sqlite3 *db, const char *sql){
  char *err = 0;
  if(sqlite3_exec(db, sql, 0, 0, &err) != SQLITE_OK){
    fprintf(stderr, "%s: %s\n", sql, err ? err : "?");
    sqlite3_free(err);
    return 0;
  }
  return 1;
}

static sqlite3 *db_open(const char *path, const char *vfs, int sealed){
  sqlite3 *db;
  char sql[160];
  if(sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, vfs) != SQLITE_OK) return 0;
  snprintf(sql, sizeof(sql), "PRAGMA key = \"x'%s%s'\";", KEYHEX, salthex);
  if(!ok(db, sql) || !ok(db, "PRAGMA cipher_page_size = 4096; PRAGMA cipher_hmac_algorithm = HMAC_SHA256;"))
    goto fail;
  if(sealed && walseal_key(db, "main", WALKEY, 32) != SQLITE_OK){ fprintf(stderr, "walseal_key failed\n"); goto fail; }
  if(!ok(db, "PRAGMA locking_mode = EXCLUSIVE; PRAGMA journal_mode = WAL;"
             "PRAGMA synchronous = FULL; PRAGMA wal_autocheckpoint = 0;"))
    goto fail;
  return db;
fail:
  sqlite3_close(db);
  return 0;
}

static void row_bytes(int seq, unsigned char *b, int n){
  uint32_t x = (uint32_t)seq * 2654435761u + 1;
  int i;
  for(i = 0; i < n; i++){ x ^= x << 13; x ^= x >> 17; x ^= x << 5; b[i] = (unsigned char)x; }
}

static int insert_rows(sqlite3 *db, int from, int n, int sz){
  sqlite3_stmt *s;
  unsigned char b[4096];
  int i, rc = 1;
  if(!ok(db, "BEGIN")) return 0;
  sqlite3_prepare_v2(db, "INSERT INTO t VALUES (?, ?)", -1, &s, 0);
  for(i = 0; i < n && rc; i++){
    row_bytes(from + i, b, sz);
    sqlite3_bind_int(s, 1, from + i);
    sqlite3_bind_blob(s, 2, b, sz, SQLITE_STATIC);
    rc = sqlite3_step(s) == SQLITE_DONE;
    sqlite3_reset(s);
  }
  sqlite3_finalize(s);
  return rc && ok(db, "COMMIT");
}

/* Rows 1..n with the right content and a clean integrity check: n, else -1. */
static int verify(const char *path, const char *vfs, int sealed, int sz){
  sqlite3 *db = db_open(path, vfs, sealed);
  sqlite3_stmt *s;
  unsigned char b[4096];
  int n = 0, bad = 0;
  if(!db) return -1;
  if(sqlite3_prepare_v2(db, "PRAGMA integrity_check", -1, &s, 0) != SQLITE_OK) bad = 1;
  else {
    if(sqlite3_step(s) != SQLITE_ROW || strcmp((const char*)sqlite3_column_text(s, 0), "ok")) bad = 1;
    sqlite3_finalize(s);
  }
  if(!bad && sqlite3_prepare_v2(db, "SELECT seq, v FROM t ORDER BY seq", -1, &s, 0) == SQLITE_OK){
    while(sqlite3_step(s) == SQLITE_ROW){
      n++;
      row_bytes(n, b, sz);
      if(sqlite3_column_int(s, 0) != n || sqlite3_column_bytes(s, 1) != sz || memcmp(sqlite3_column_blob(s, 1), b, sz)){ bad = 1; break; }
    }
    sqlite3_finalize(s);
  } else bad = 1;
  sqlite3_close(db);
  return bad ? -1 : n;
}

static void rm_db(const char *path){
  char w[512];
  snprintf(w, sizeof(w), "%s-wal", path);
  unlink(path); unlink(w);
}

/* ---- io ------------------------------------------------------------------ */

static void print_stat(const char *label){
  static const char *names[] = { "db ", "wal" };
  int f;
  for(f = 0; f < 2; f++){
    Stat *s = &st[f];
    printf("  %-11s %s  writes %6lld  %8.2f MB  unaligned %6lld  partial-blocks %6lld  | combined: runs %5lld partial %5lld"
           "  | reads %6lld  %7.2f MB  unaligned %6lld  blocks %6lld  | syncs %lld\n",
           f ? "" : label, names[f], s->nW, s->bW / 1048576.0, s->nWun, s->partW, s->runs, s->runPart,
           s->nR, s->bR / 1048576.0, s->nRun, s->blkR, s->nS);
  }
}

static int cmd_io(const char *dir, int rows, int txn, int ckpt){
  static const struct { const char *label, *vfs; int sealed; } modes[] = {
    { "stock", "count", 0 }, { "sealed", "walseal", 1 }
  };
  Stat seen[2][3];
  int m, f;
  printf("io: %d rows of 1000 bytes, %d rows/txn, synchronous=FULL, checkpoint(RESTART) every %d commits\n", rows, txn, ckpt);
  for(m = 0; m < 2; m++){
    char path[512];
    sqlite3 *db;
    int i, c = 0;
    snprintf(path, sizeof(path), "%s/io-%s.db", dir, modes[m].label);
    rm_db(path);
    if(!(db = db_open(path, modes[m].vfs, modes[m].sealed)) || !ok(db, "CREATE TABLE t(seq INTEGER PRIMARY KEY, v BLOB)")) return 1;
    memset(st, 0, sizeof(st));
    for(i = 1; i <= rows; i += txn){
      if(!insert_rows(db, i, i + txn > rows + 1 ? rows + 1 - i : txn, 1000)) return 1;
      if(++c % ckpt == 0 && sqlite3_wal_checkpoint_v2(db, 0, SQLITE_CHECKPOINT_RESTART, 0, 0) != SQLITE_OK) return 1;
    }
    sqlite3_wal_checkpoint_v2(db, 0, SQLITE_CHECKPOINT_RESTART, 0, 0);
    print_stat(modes[m].label);
    memcpy(seen[m], st, sizeof(st));
    sqlite3_close(db);
    if(verify(path, modes[m].vfs, modes[m].sealed, 1000) != rows){ fprintf(stderr, "%s: verify failed\n", modes[m].label); return 1; }
  }
  /* Same layout, writes and syncs as stock: every call, byte and sync. */
  for(f = 0; f < 2; f++){
    Stat *a = &seen[0][f], *b = &seen[1][f];
    if(a->nW != b->nW || a->bW != b->bW || a->nWun != b->nWun || a->nS != b->nS || a->bR != b->bR){
      fprintf(stderr, "io: sealed %s I/O differs from stock\n", f ? "wal" : "db");
      return 1;
    }
  }
  printf("io: sealed I/O identical to stock\n");
  return 0;
}

/* ---- leak ---------------------------------------------------------------- */

static uint32_t be32(const unsigned char *a){ return (uint32_t)a[0] << 24 | (uint32_t)a[1] << 16 | (uint32_t)a[2] << 8 | a[3]; }

static unsigned char *slurp(const char *path, long *n){
  FILE *f = fopen(path, "rb");
  unsigned char *b;
  if(!f){ *n = 0; return 0; }
  fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
  b = malloc(*n ? *n : 1);
  if(fread(b, 1, *n, f) != (size_t)*n) *n = 0;
  fclose(f);
  return b;
}

/* Frames whose header reads, without the key, as a plausible page number
** (1..1000; these databases have under 100 pages) at a 4 KB page size: in
** the clear that is every frame.  An enciphered header lands there by chance
** with probability 2^-22 per frame. */
static int clear_pgnos(const unsigned char *b, long n){
  long off;
  int k = 0;
  for(off = 32; off + 24 + 4096 <= n; off += 24 + 4096){
    uint32_t pg = be32(b + off);
    if(pg >= 1 && pg <= 1000) k++;
  }
  return k;
}

/* Prints what the WAL shows without the key; returns how many frames
** it can read (-1: no WAL header in the clear). */
static int dump_wal(const char *path, int *pgnos, long *size){
  long n, off;
  unsigned char *b = slurp(path, &n);
  int fr = 0, shown = 0;
  *pgnos = b ? clear_pgnos(b, n) : 0;
  *size = n;
  if(n < 32 || (be32(b) & 0xfffffffe) != 0x377f0682){
    printf("    %ld bytes, no SQLite WAL header (starts %.8s...), %d frame headers with a plausible page number\n",
           n, b ? (const char*)b : "", *pgnos);
    free(b);
    return -1;
  }
  printf("    %ld bytes, WAL magic %08x, page size %u, checkpoint seq %u, salts %08x %08x\n",
         n, be32(b), be32(b + 8), be32(b + 12), be32(b + 16), be32(b + 20));
  printf("    frames (pgno, '*'=commit with db size):");
  for(off = 32; off + 24 + 4096 <= n; off += 24 + 4096, fr++){
    uint32_t pg = be32(b + off), commit = be32(b + off + 4);
    if(be32(b + off + 8) != be32(b + 16)) break;
    if(shown++ < 60) printf(commit ? " %u*%u" : " %u", pg, commit);
  }
  printf("%s\n    %d frames readable without the key\n", shown > 60 ? " ..." : "", fr);
  free(b);
  return fr;
}

static int cmd_leak(const char *dir){
  int m, fails = 0;
  for(m = 0; m < 2; m++){
    char path[512], wal[520];
    sqlite3 *db;
    int i, fr, pgnos;
    long size;
    snprintf(path, sizeof(path), "%s/leak-%s.db", dir, m ? "sealed" : "stock");
    snprintf(wal, sizeof(wal), "%s-wal", path);
    rm_db(path);
    if(!(db = db_open(path, m ? "walseal" : "count", m)) || !ok(db, "CREATE TABLE t(seq INTEGER PRIMARY KEY, v BLOB);"
       "CREATE TABLE secret(name TEXT PRIMARY KEY, balance INTEGER)")) return 1;
    for(i = 1; i <= 200; i += 20) if(!insert_rows(db, i, 20, 1000)) return 1;
    sqlite3_wal_checkpoint_v2(db, 0, SQLITE_CHECKPOINT_RESTART, 0, 0);
    /* activity an observer might care about: three updates of one row */
    ok(db, "INSERT INTO secret VALUES ('alice', 100)");
    for(i = 0; i < 3; i++) ok(db, "UPDATE secret SET balance = balance + 1");
    if(!insert_rows(db, 201, 20, 1000)) return 1;
    printf("%s WAL while open (after a checkpoint, 1 insert + 3 one-row updates + 20 row inserts):\n", m ? "sealed" : "stock");
    fr = dump_wal(wal, &pgnos, &size);
    sqlite3_close(db);
    /* stock shows the leak (the check can see one); sealed shows nothing */
    if(m == 0 && (fr <= 0 || pgnos == 0)){ fprintf(stderr, "leak: stock WAL should be readable\n"); fails++; }
    if(m == 1 && (size < 32 || fr != -1 || pgnos != 0)){ fprintf(stderr, "leak: sealed WAL shows its headers\n"); fails++; }
  }
  if(!fails) printf("leak: sealed WAL shows no header field in the clear\n");
  return fails != 0;
}

/* ---- crash --------------------------------------------------------------- */

typedef struct Mark { int op, lastSync, rows; } Mark;

static unsigned rnd_state = 1;
static unsigned rnd(void){ rnd_state ^= rnd_state << 13; rnd_state ^= rnd_state >> 17; rnd_state ^= rnd_state << 5; return rnd_state; }

static void put_file(const char *path, const unsigned char *b, long n){
  int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
  if(n && write(fd, b, n) != n) perror("write");
  close(fd);
}

/* how: 0 apply, 1 skip, 2 garble the 4 KB blocks it touches */
static void apply(const char *paths[2], const Op *o, int how){
  int fd;
  if(how == 1 || o->kind == 'S') return;
  fd = open(paths[o->fid], O_RDWR | O_CREAT, 0644);
  if(o->kind == 'T'){ if(ftruncate(fd, o->off)) perror("ftruncate"); }
  else if(how == 0){ if(pwrite(fd, o->d, o->n, o->off) != o->n) perror("pwrite"); }
  else {
    i64 b0 = o->off / BLK * BLK, b1 = (o->off + o->n + BLK - 1) / BLK * BLK, i;
    unsigned char *g = malloc(b1 - b0);
    for(i = 0; i < b1 - b0; i++) g[i] = (unsigned char)rnd();
    if(pwrite(fd, g, b1 - b0, b0) != b1 - b0) perror("pwrite");
    free(g);
  }
  close(fd);
}

static int cmd_crash(const char *dir, const char *mode, unsigned seed){
  const char *vfs = !strcmp(mode, "stock") ? "count" : "walseal";
  int sealed = strcmp(mode, "stock") != 0, sz = 700;
  char path[512], wal[520], rpath[512], rwal[520];
  const char *rp[2];
  unsigned char *snapDb, *snapWal;
  long nDb, nWal;
  Mark *marks;
  int nMarks = 0, rows, c, i, fails = 0, checks = 0;
  sqlite3 *db;

  rnd_state = seed ? seed : 1;
  snprintf(path, sizeof(path), "%s/crash-%s.db", dir, mode);
  snprintf(wal, sizeof(wal), "%s-wal", path);
  snprintf(rpath, sizeof(rpath), "%s/replay-%s.db", dir, mode);
  snprintf(rwal, sizeof(rwal), "%s-wal", rpath);
  rp[0] = rpath; rp[1] = rwal;
  rm_db(path);

  /* starting state: 40 rows, checkpointed, then the files as they are */
  if(!(db = db_open(path, vfs, sealed)) || !ok(db, "CREATE TABLE t(seq INTEGER PRIMARY KEY, v BLOB)")) return 1;
  rows = 40;
  if(!insert_rows(db, 1, rows, sz)) return 1;
  sqlite3_wal_checkpoint_v2(db, 0, SQLITE_CHECKPOINT_RESTART, 0, 0);
  snapDb = slurp(path, &nDb);
  snapWal = slurp(wal, &nWal);
  if(getenv("SALT_IN_KEY")) for(i = 0; i < 16; i++) snprintf(salthex + 2*i, 3, "%02x", snapDb[i]);

  /* recorded run: commits of 1-40 rows; checkpoints late enough that the
  ** first WAL generation spans more than one walseal group (168 frames) */
  marks = malloc(sizeof(Mark) * 100);
  marks[nMarks].op = 0; marks[nMarks].lastSync = -1; marks[nMarks++].rows = rows;
  recording = 1; nOps = 0;
  for(c = 1; c <= 60; c++){
    int k = 1 + rnd() % 40, last = -1;
    if(!insert_rows(db, rows + 1, k, sz)) return 1;
    rows += k;
    for(i = nOps - 1; i >= 0; i--) if(ops[i].kind == 'S'){ last = i; break; }
    marks[nMarks].op = nOps; marks[nMarks].lastSync = last; marks[nMarks++].rows = rows;
    if((c == 40 || c == 52) && sqlite3_wal_checkpoint_v2(db, 0, SQLITE_CHECKPOINT_RESTART, 0, 0) != SQLITE_OK) return 1;
  }
  recording = 0;
  sqlite3_close(db);
  {
    i64 mx = 0;
    for(i = 0; i < nOps; i++) if(ops[i].fid == F_WAL && ops[i].kind == 'W' && ops[i].off + ops[i].n > mx) mx = ops[i].off + ops[i].n;
    printf("crash %s: %d ops recorded (%d commits, %d rows), WAL reaches %lld bytes\n", mode, nOps, nMarks - 1, rows, mx);
  }

  /* every cut point: prefix + {clean, torn (garbled), reordered since the last sync} */
  for(c = 0; c <= nOps; c++){
    int variant, lastSync = -1, durable = 0, upper = rows, j;
    for(i = c - 1; i >= 0; i--) if(ops[i].kind == 'S'){ lastSync = i; break; }
    for(j = 0; j < nMarks; j++) if(marks[j].lastSync < c && marks[j].lastSync >= 0 && marks[j].op <= c) durable = marks[j].rows;
    durable = durable ? durable : 40;
    for(j = 0; j < nMarks; j++) if(marks[j].op >= c){ upper = marks[j].rows; break; }
    for(variant = 0; variant < 4; variant++){
      int n, good = 0;
      if(variant == 1 && (c == nOps || ops[c].kind != 'W')) continue;
      put_file(rpath, snapDb, nDb);
      if(snapWal) put_file(rwal, snapWal, nWal); else unlink(rwal);
      char hows[4096]; int nh = 0;
      for(i = 0; i < c; i++){
        int how = 0;
        if(variant >= 2 && i > lastSync){ unsigned r = rnd() % 10; how = r < 5 ? 0 : r < 8 ? 1 : 2; }
        if(i > lastSync && nh < 4000) nh += snprintf(hows + nh, sizeof(hows) - nh, "\n    op %d %c fid %d off %lld n %d: %s",
                                                  i, ops[i].kind, ops[i].fid, ops[i].off, ops[i].n, how == 0 ? "apply" : how == 1 ? "skip" : "GARBLE");
        apply(rp, &ops[i], how);
      }
      hows[nh] = 0;
      if(variant == 1) apply(rp, &ops[c], 2);
      n = verify(rpath, vfs, sealed, sz);
      checks++;
      if(n >= durable && n <= upper)
        for(j = 0; j < nMarks; j++) if(marks[j].rows == n) good = 1;
      if(!good){
        if(fails++ < 10) printf("  FAIL cut %d variant %d (op %c fid %d off %lld n %d): rows %d, want a commit boundary in [%d, %d]\n",
                                c, variant, c < nOps ? ops[c].kind : '-', c < nOps ? ops[c].fid : -1,
                                c < nOps ? ops[c].off : -1, c < nOps ? ops[c].n : 0, n, durable, upper);
        if(getenv("CRASH_DEBUG") && fails == 1){
          char cmd[1200];
          printf("  last sync op %d; ops since:%s\n", lastSync, hows);
          snprintf(cmd, sizeof(cmd), "cp %s %s.fail; cp %s %s.fail", rpath, rpath, rwal, rwal);
          if(system(cmd)) {}
        }
      }
    }
  }
  printf("crash %s%s: %d replays, %d failures\n", mode, salthex[0] ? " (salt in key)" : "", checks, fails);
  return fails != 0;
}

int main(int argc, char **argv){
  sqlite3_initialize();
  count_register();
  if(walseal_register("count", 0) != SQLITE_OK) return 2;
  if(argc >= 3 && !strcmp(argv[1], "io"))
    return cmd_io(argv[2], argc > 3 ? atoi(argv[3]) : 1000, argc > 4 ? atoi(argv[4]) : 16, argc > 5 ? atoi(argv[5]) : 20);
  if(argc >= 3 && !strcmp(argv[1], "leak")) return cmd_leak(argv[2]);
  if(argc >= 4 && !strcmp(argv[1], "crash")) return cmd_crash(argv[2], argv[3], argc > 4 ? (unsigned)atoi(argv[4]) : 1);
  fprintf(stderr, "usage: walseal_test io|leak|crash <dir> ...\n");
  return 2;
}
