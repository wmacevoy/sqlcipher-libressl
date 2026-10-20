/*
** walseal.c -- a VFS shim that encrypts the WAL's headers in place
** (walseal.h).
**
** Provenance: copied from the maintainer's sqlcipher-embedded (BSD, same
** author), branch experiment/wal-seal, commit 5d50c676,
** embedded/sqlcipher/walseal.c.  Changed here: PSA Crypto -> LibreSSL's
** libcrypto (EVP AES-256-ECB without padding, HMAC-SHA256 via HMAC()), and
** walseal_key(db, zDb, NULL, 0), which lets an unencrypted database's WAL
** pass through.  The cipher, the tweaks and the header regions are
** unchanged, so the source's crash-replay findings still hold.
**
** SQLCipher encrypts the page images in the WAL; SQLite writes the 32-byte
** WAL header and each frame's 24-byte header (page number, commit size,
** salts, checksums) in the clear.  walseal encrypts exactly those bytes,
** where they are: the file's layout, sizes, offsets, writes and syncs are
** stock, so SQLite's padding, recovery and crash behaviour are unchanged.
**
** There is no room for an IV or a MAC, so a header is enciphered with a
** length-preserving tweaked pseudo-random permutation: a 4-round Feistel
** network over its two halves, round function F_r(x) = AES(AES(T_r) ^ x)
** (a CBC-MAC), T_r = (position, piece, r, length, the WAL's salts).  The
** salts change with every WAL generation, so a header is enciphered
** differently each time its slot is reused; deterministic encryption then
** reveals only an identical header rewritten in place.  A tampered or torn
** header deciphers to noise, fails wal.c's checksum and ends recovery
** there, as a torn plaintext header does.
**
** Pieces: with synchronous=FULL and no POWERSAFE_OVERWRITE, wal.c pads a
** commit to the next sector boundary and syncs there, which can split a
** frame header: part written before the sync, the rest after.  Rewriting
** the whole header after the sync would touch the synced sector, the
** very thing the padding avoids.  So a header that crosses a sector
** boundary is two independently enciphered pieces (8 + 16 or 16 + 8 bytes:
** header offsets are multiples of 8), and every write touches exactly the
** bytes it would touch unsealed.
**
** All-zero ciphertext is left as zeros, so never-written (preallocated)
** space reads as "no frame" exactly as before.
*/
#include <stdint.h>
#include <string.h>
#include "sqlite3.h"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/crypto.h>
#include "walseal.h"

#define WS_FCNTL_KEY  0x5753454c          /* 'WSEL' */
#define WS_FCNTL_SRC  0x57534b53          /* 'WSKS' */
#define WS_LHDR       32                  /* WAL header */
#define WS_FHDR       24                  /* frame header */

typedef sqlite3_int64 i64;
typedef uint8_t u8;

typedef struct WsVfs { sqlite3_vfs base; sqlite3_vfs *under; } WsVfs;

typedef struct WsFile WsFile;
struct WsFile {
  sqlite3_file base;
  sqlite3_file *real;          /* the file of the VFS below, after this struct */
  int isMain;
  /* main database */
  const char *zName;           /* valid until xClose (SQLite's guarantee) */
  EVP_CIPHER_CTX *aes;         /* AES-256 (ECB: the PRP's block cipher) */
  int keyed;
  int plain;                   /* walseal_key(..., NULL, 0): the WAL passes through */
  int (*xKey)(void*, unsigned char*, int*);   /* walseal_key_source, or 0 */
  void *pKeyArg;
  WsFile *pNext;               /* open main databases */
  /* WAL */
  WsFile *pMain;
  int P;                       /* page size from the WAL header; 0 until known */
  int B;                       /* sector size: header pieces split at its multiples */
  u8 salt[8];                  /* the WAL header's salts (tweak) */
};

static WsFile *ws_mains;       /* across all walseal VFSes */

#define REAL(p) ((p)->real)
#define RM(p)   ((p)->real->pMethods)

/* ---- the tweaked PRP ----------------------------------------------------- */

/* ECB without padding keeps no state between calls, so one context serves
** every call; n is a multiple of 16. */
static int aes(WsFile *m, const u8 *in, u8 *out, size_t n){
  int olen = 0;
  return EVP_EncryptUpdate(m->aes, out, &olen, in, (int)n) == 1 && olen == (int)n
         ? SQLITE_OK : SQLITE_ERROR;
}

static void swap(u8 *a, u8 *b, int h){ u8 s[16]; memcpy(s, a, h); memcpy(a, b, h); memcpy(b, s, h); }

/* Encipher (enc 1) or decipher (enc 0) n bytes (even, 8..32) in place. */
static int ws_prp(WsFile *m, u8 *x, int n, uint64_t tweak, const u8 *salt, int enc){
  u8 t[4][16], f[16], *L = x, *R = x + n/2;
  int h = n/2, r, i;
  for(r = 0; r < 4; r++){
    for(i = 0; i < 6; i++) t[r][i] = (u8)(tweak >> (8*i));
    t[r][6] = (u8)r;
    t[r][7] = (u8)n;
    memcpy(&t[r][8], salt, 8);
  }
  if(aes(m, t[0], t[0], sizeof(t))) return SQLITE_ERROR;
  for(r = 0; r < 4; r++){
    int k = enc ? r : 3 - r;
    if(!enc) swap(L, R, h);
    memcpy(f, t[k], 16);
    for(i = 0; i < h; i++) f[i] ^= R[i];
    if(aes(m, f, f, 16)) return SQLITE_ERROR;
    for(i = 0; i < h; i++) L[i] ^= f[i];
    if(enc) swap(L, R, h);
  }
  return SQLITE_OK;
}

static int all_zero(const u8 *a, int n){ while(n--) if(*a++) return 0; return 1; }

/* key = HMAC-SHA256(master, "walseal hdr"), as an AES-256 key */
static void ws_dropkey(WsFile *m){
  EVP_CIPHER_CTX_free(m->aes);    /* wipes the key schedule */
  m->aes = 0;
  m->keyed = 0;
  m->plain = 0;
}

/* master NULL and n 0: no key, the WAL passes through (an unencrypted
** database: its pages are in the clear anyway). */
static int ws_setkey(WsFile *m, const u8 *master, int n){
  u8 k[32];
  unsigned int len = 0;
  int rc;
  if(!master && n == 0){ ws_dropkey(m); m->plain = 1; return SQLITE_OK; }
  if(n < 16) return SQLITE_MISUSE;
  ws_dropkey(m);
  rc = HMAC(EVP_sha256(), master, n, (const u8*)"walseal hdr", 11, k, &len) && len == 32
       ? SQLITE_OK : SQLITE_ERROR;
  if(rc == SQLITE_OK){
    rc = (m->aes = EVP_CIPHER_CTX_new()) != 0
         && EVP_EncryptInit_ex(m->aes, EVP_aes_256_ecb(), 0, k, 0) == 1
         && EVP_CIPHER_CTX_set_padding(m->aes, 0) == 1
         ? SQLITE_OK : SQLITE_ERROR;
    if(rc) ws_dropkey(m);
  }
  OPENSSL_cleanse(k, sizeof(k));
  m->keyed = rc == SQLITE_OK;
  return rc;
}

/* A new WAL begins: take the key from the key source, if there is one.  The
** WAL holds no frames yet, so no header sealed under the old key is left. */
static int ws_rekey(WsFile *m){
  u8 k[64];
  int n = 0, rc;
  if(!m->xKey) return SQLITE_OK;
  rc = m->xKey(m->pKeyArg, k, &n);
  if(rc == SQLITE_OK) rc = n >= 16 && n <= (int)sizeof(k) ? ws_setkey(m, k, n) : SQLITE_ERROR;
  OPENSSL_cleanse(k, sizeof(k));
  return rc;
}

/* ---- WAL: the header regions of a byte range --------------------------- */

typedef struct Region { i64 off; int n; uint64_t tweak; } Region;

/* The first header piece ending after file offset `from`: the WAL header
** (tweak 0), then frame i's header, split where it crosses a multiple of
** the sector size (tweaks 2(i+1) and 2(i+1)+1).  0 when there is none
** (frame headers need the page size). */
static int region_at(WsFile *p, i64 from, Region *g){
  i64 fsz, i, off, split;
  if(from < WS_LHDR){ g->off = 0; g->n = WS_LHDR; g->tweak = 0; return 1; }
  if(!p->P) return 0;
  fsz = WS_FHDR + p->P;
  i = (from - WS_LHDR) / fsz;
  if(WS_LHDR + i * fsz + WS_FHDR <= from) i++;
  off = WS_LHDR + i * fsz;
  split = (off / p->B + 1) * p->B;
  if(split >= off + WS_FHDR || (split - off) % 2){       /* whole */
    g->off = off; g->n = WS_FHDR; g->tweak = (uint64_t)(i + 1) << 1;
  }else if(from < split){
    g->off = off; g->n = (int)(split - off); g->tweak = (uint64_t)(i + 1) << 1;
  }else{
    g->off = split; g->n = (int)(off + WS_FHDR - split); g->tweak = (uint64_t)(i + 1) << 1 | 1;
  }
  return 1;
}
static int region_next(WsFile *p, Region *g){ return region_at(p, g->off + g->n, g); }
#define region_first region_at

static const u8 no_salt[8];
static const u8 *tweak_salt(WsFile *p, const Region *g){ return g->tweak ? p->salt : no_salt; }

/* From a plaintext WAL header: page size and salts. */
static void learn_hdr(WsFile *p, const u8 *hdr){
  uint32_t P = (uint32_t)hdr[8] << 24 | (uint32_t)hdr[9] << 16 | (uint32_t)hdr[10] << 8 | hdr[11];
  if(P >= 512 && P <= 65536 && !(P & (P - 1))) p->P = (int)P;
  memcpy(p->salt, hdr + 16, 8);
}

/* Decipher, in buf = file bytes [off, off+n), the pieces it holds whole. */
static int decipher_whole(WsFile *p, u8 *buf, i64 off, int n){
  Region g;
  int ok;
  for(ok = region_first(p, off, &g); ok && g.off < off + n; ok = region_next(p, &g)){
    u8 *x = buf + (g.off - off);
    if(g.off < off || g.off + g.n > off + n || all_zero(x, g.n)) continue;
    if(ws_prp(p->pMain, x, g.n, g.tweak, tweak_salt(p, &g), 0)) return SQLITE_IOERR;
    if(g.tweak == 0) learn_hdr(p, x);
  }
  return SQLITE_OK;
}

/* Reads come back with the headers deciphered; a header the read cuts is
** read whole on its own and the requested part copied out. */
static int wsWalRead(sqlite3_file *f, void *zBuf, int iAmt, i64 iOfst){
  WsFile *p = (WsFile*)f;
  u8 *z = zBuf;
  Region g;
  int ok, rc2, rc = RM(p)->xRead(REAL(p), zBuf, iAmt, iOfst);
  if(rc != SQLITE_OK && rc != SQLITE_IOERR_SHORT_READ) return rc;
  if((rc2 = decipher_whole(p, z, iOfst, iAmt))) return rc2;
  for(ok = region_first(p, iOfst, &g); ok && g.off < iOfst + iAmt; ok = region_next(p, &g)){
    u8 x[WS_LHDR];
    i64 a, b;
    if(g.off >= iOfst && g.off + g.n <= iOfst + iAmt) continue;
    rc2 = RM(p)->xRead(REAL(p), x, g.n, g.off);
    if(rc2 != SQLITE_OK && rc2 != SQLITE_IOERR_SHORT_READ) return rc2;
    if(!all_zero(x, g.n) && ws_prp(p->pMain, x, g.n, g.tweak, tweak_salt(p, &g), 0)) return SQLITE_IOERR_READ;
    a = g.off > iOfst ? g.off : iOfst;
    b = g.off + g.n < iOfst + iAmt ? g.off + g.n : iOfst + iAmt;
    memcpy(z + (a - iOfst), x + (a - g.off), (size_t)(b - a));
  }
  return rc;
}

/* Writes go out with the headers enciphered; a write that cuts a header is
** widened to cover it (read, decipher, patch). */
static int wsWalWrite(sqlite3_file *f, const void *zBuf, int iAmt, i64 iOfst){
  WsFile *p = (WsFile*)f;
  i64 a = iOfst, b = iOfst + iAmt;
  Region g;
  u8 *buf;
  int ok, rc;
  if(iOfst == 0 && iAmt >= WS_LHDR){
    if(ws_rekey(p->pMain)) return SQLITE_IOERR_WRITE;
    learn_hdr(p, zBuf);
  }
  for(ok = region_first(p, iOfst, &g); ok && g.off < b; ok = region_next(p, &g)){
    if(g.off < a) a = g.off;
    if(g.off + g.n > b) b = g.off + g.n;
  }
  if(!(buf = sqlite3_malloc64((sqlite3_uint64)(b - a)))) return SQLITE_NOMEM;
  if(a < iOfst || b > iOfst + iAmt){
    rc = RM(p)->xRead(REAL(p), buf, (int)(b - a), a);
    if(rc == SQLITE_OK || rc == SQLITE_IOERR_SHORT_READ) rc = decipher_whole(p, buf, a, (int)(b - a));
    if(rc){ sqlite3_free(buf); return rc; }
  }
  memcpy(buf + (iOfst - a), zBuf, (size_t)iAmt);
  for(ok = region_first(p, a, &g); ok && g.off < b; ok = region_next(p, &g)){
    u8 *x = buf + (g.off - a);
    if(all_zero(x, g.n)) continue;
    if(ws_prp(p->pMain, x, g.n, g.tweak, tweak_salt(p, &g), 1)){ sqlite3_free(buf); return SQLITE_IOERR_WRITE; }
  }
  rc = RM(p)->xWrite(REAL(p), buf, (int)(b - a), a);
  sqlite3_free(buf);
  return rc;
}

/* ---- pass-through ------------------------------------------------------ */

static int wsClose(sqlite3_file *f){
  WsFile *p = (WsFile*)f;
  if(p->isMain){
    WsFile **pp;
    sqlite3_mutex *mx = sqlite3_mutex_alloc(SQLITE_MUTEX_STATIC_VFS2);
    sqlite3_mutex_enter(mx);
    for(pp = &ws_mains; *pp; pp = &(*pp)->pNext) if(*pp == p){ *pp = p->pNext; break; }
    sqlite3_mutex_leave(mx);
    ws_dropkey(p);
  }
  return RM(p)->xClose(REAL(p));
}
static int wsRead(sqlite3_file *f, void *z, int n, i64 o){ return RM((WsFile*)f)->xRead(REAL((WsFile*)f), z, n, o); }
static int wsWrite(sqlite3_file *f, const void *z, int n, i64 o){ return RM((WsFile*)f)->xWrite(REAL((WsFile*)f), z, n, o); }
static int wsTruncate(sqlite3_file *f, i64 s){ return RM((WsFile*)f)->xTruncate(REAL((WsFile*)f), s); }
static int wsSync(sqlite3_file *f, int fl){ return RM((WsFile*)f)->xSync(REAL((WsFile*)f), fl); }
static int wsFileSize(sqlite3_file *f, i64 *s){ return RM((WsFile*)f)->xFileSize(REAL((WsFile*)f), s); }
static int wsLock(sqlite3_file *f, int l){ return RM((WsFile*)f)->xLock(REAL((WsFile*)f), l); }
static int wsUnlock(sqlite3_file *f, int l){ return RM((WsFile*)f)->xUnlock(REAL((WsFile*)f), l); }
static int wsCheckReservedLock(sqlite3_file *f, int *r){ return RM((WsFile*)f)->xCheckReservedLock(REAL((WsFile*)f), r); }
static int wsFileControl(sqlite3_file *f, int op, void *arg){
  WsFile *p = (WsFile*)f;
  if(op == WS_FCNTL_KEY && p->isMain){
    const void **a = arg;
    return ws_setkey(p, a[0], *(const int*)a[1]);
  }
  if(op == WS_FCNTL_SRC && p->isMain){
    void **a = arg;
    p->xKey = (int(*)(void*, unsigned char*, int*))a[0];
    p->pKeyArg = a[1];
    return SQLITE_OK;
  }
  return RM(p)->xFileControl(REAL(p), op, arg);
}
static int wsSectorSize(sqlite3_file *f){ return RM((WsFile*)f)->xSectorSize(REAL((WsFile*)f)); }
static int wsDeviceCharacteristics(sqlite3_file *f){ return RM((WsFile*)f)->xDeviceCharacteristics(REAL((WsFile*)f)); }
static int wsShmMap(sqlite3_file *f, int i, int sz, int ext, void volatile **pp){ return RM((WsFile*)f)->xShmMap(REAL((WsFile*)f), i, sz, ext, pp); }
static int wsShmLock(sqlite3_file *f, int o, int n, int fl){ return RM((WsFile*)f)->xShmLock(REAL((WsFile*)f), o, n, fl); }
static void wsShmBarrier(sqlite3_file *f){ RM((WsFile*)f)->xShmBarrier(REAL((WsFile*)f)); }
static int wsShmUnmap(sqlite3_file *f, int del){ return RM((WsFile*)f)->xShmUnmap(REAL((WsFile*)f), del); }
static int wsFetch(sqlite3_file *f, i64 o, int n, void **pp){
  WsFile *p = (WsFile*)f;
  if(RM(p)->iVersion < 3){ *pp = 0; return SQLITE_OK; }
  return RM(p)->xFetch(REAL(p), o, n, pp);
}
static int wsUnfetch(sqlite3_file *f, i64 o, void *pp){
  WsFile *p = (WsFile*)f;
  return RM(p)->iVersion < 3 ? SQLITE_OK : RM(p)->xUnfetch(REAL(p), o, pp);
}

static const sqlite3_io_methods ws_io_pass1 = {
  1, wsClose, wsRead, wsWrite, wsTruncate, wsSync, wsFileSize, wsLock, wsUnlock,
  wsCheckReservedLock, wsFileControl, wsSectorSize, wsDeviceCharacteristics,
  0, 0, 0, 0, 0, 0
};
static const sqlite3_io_methods ws_io_pass3 = {
  3, wsClose, wsRead, wsWrite, wsTruncate, wsSync, wsFileSize, wsLock, wsUnlock,
  wsCheckReservedLock, wsFileControl, wsSectorSize, wsDeviceCharacteristics,
  wsShmMap, wsShmLock, wsShmBarrier, wsShmUnmap, wsFetch, wsUnfetch
};
/* The WAL file itself is never mapped and has no shared memory. */
static const sqlite3_io_methods ws_io_wal = {
  1, wsClose, wsWalRead, wsWalWrite, wsTruncate, wsSync, wsFileSize,
  wsLock, wsUnlock, wsCheckReservedLock, wsFileControl, wsSectorSize, wsDeviceCharacteristics,
  0, 0, 0, 0, 0, 0
};

/* ---- VFS ----------------------------------------------------------------- */

#define UNDER(v) (((WsVfs*)(v))->under)

static int wsOpen(sqlite3_vfs *vfs, const char *zName, sqlite3_file *f, int flags, int *pOutFlags){
  WsFile *p = (WsFile*)f;
  int rc;
  memset(p, 0, sizeof(*p));
  p->real = (sqlite3_file*)&p[1];
  if(flags & SQLITE_OPEN_WAL){
    const char *zDb = sqlite3_filename_database(zName);
    sqlite3_mutex *mx = sqlite3_mutex_alloc(SQLITE_MUTEX_STATIC_VFS2);
    WsFile *m;
    sqlite3_mutex_enter(mx);
    for(m = ws_mains; m && strcmp(m->zName, zDb); m = m->pNext){}
    sqlite3_mutex_leave(mx);
    if(!m || (!m->keyed && !m->plain)){
      sqlite3_log(SQLITE_CANTOPEN, "walseal: no key for %s (walseal_key)", zDb);
      return SQLITE_CANTOPEN;
    }
    p->pMain = m;
  }
  rc = UNDER(vfs)->xOpen(UNDER(vfs), zName, p->real, flags, pOutFlags);
  if(rc){ p->real->pMethods = 0; return rc; }
  if(flags & SQLITE_OPEN_WAL){
    int B = p->real->pMethods->xSectorSize(p->real);     /* as sqlite3SectorSize() */
    p->B = B < 32 ? 512 : B > 65536 ? 65536 : B;
    p->base.pMethods = p->pMain->plain ? &ws_io_pass1 : &ws_io_wal;
    return SQLITE_OK;
  }
  p->base.pMethods = p->real->pMethods->iVersion >= 2 ? &ws_io_pass3 : &ws_io_pass1;
  if(flags & SQLITE_OPEN_MAIN_DB){
    sqlite3_mutex *mx = sqlite3_mutex_alloc(SQLITE_MUTEX_STATIC_VFS2);
    p->isMain = 1;
    p->zName = zName;
    sqlite3_mutex_enter(mx);
    p->pNext = ws_mains;
    ws_mains = p;
    sqlite3_mutex_leave(mx);
  }
  return SQLITE_OK;
}

static int wsDelete(sqlite3_vfs *v, const char *z, int s){ return UNDER(v)->xDelete(UNDER(v), z, s); }
static int wsAccess(sqlite3_vfs *v, const char *z, int fl, int *r){ return UNDER(v)->xAccess(UNDER(v), z, fl, r); }
static int wsFullPathname(sqlite3_vfs *v, const char *z, int n, char *o){ return UNDER(v)->xFullPathname(UNDER(v), z, n, o); }
static int wsRandomness(sqlite3_vfs *v, int n, char *o){ return UNDER(v)->xRandomness(UNDER(v), n, o); }
static int wsSleep(sqlite3_vfs *v, int us){ return UNDER(v)->xSleep(UNDER(v), us); }
static int wsCurrentTime(sqlite3_vfs *v, double *t){ return UNDER(v)->xCurrentTime(UNDER(v), t); }
static int wsGetLastError(sqlite3_vfs *v, int n, char *z){ return UNDER(v)->xGetLastError(UNDER(v), n, z); }
static int wsCurrentTimeInt64(sqlite3_vfs *v, i64 *t){
  sqlite3_vfs *u = UNDER(v);
  if(u->iVersion >= 2 && u->xCurrentTimeInt64) return u->xCurrentTimeInt64(u, t);
  { double d; int rc = u->xCurrentTime(u, &d); *t = (i64)(d * 86400000.0); return rc; }
}

int walseal_register_as(const char *zName, const char *zUnder){
  sqlite3_vfs *under = sqlite3_vfs_find(zUnder);
  WsVfs *v;
  if(!under) return SQLITE_NOTFOUND;
  if(!(v = sqlite3_malloc(sizeof(*v)))) return SQLITE_NOMEM;   /* lives as long as the process */
  memset(v, 0, sizeof(*v));
  v->under = under;
  v->base.iVersion = 2;
  v->base.szOsFile = (int)sizeof(WsFile) + under->szOsFile;
  v->base.mxPathname = under->mxPathname;
  v->base.zName = zName;
  v->base.xOpen = wsOpen;
  v->base.xDelete = wsDelete;
  v->base.xAccess = wsAccess;
  v->base.xFullPathname = wsFullPathname;
  v->base.xRandomness = wsRandomness;
  v->base.xSleep = wsSleep;
  v->base.xCurrentTime = wsCurrentTime;
  v->base.xGetLastError = wsGetLastError;
  v->base.xCurrentTimeInt64 = wsCurrentTimeInt64;
  return sqlite3_vfs_register(&v->base, 0);
}

int walseal_register(const char *zUnder, int makeDefault){
  int rc = walseal_register_as("walseal", zUnder);
  if(rc == SQLITE_OK && makeDefault) rc = sqlite3_vfs_register(sqlite3_vfs_find("walseal"), 1);
  return rc;
}

int walseal_key(sqlite3 *db, const char *zDb, const void *key, int nKey){
  const void *a[2];
  a[0] = key;
  a[1] = &nKey;
  return sqlite3_file_control(db, zDb, WS_FCNTL_KEY, (void*)a);
}

int walseal_key_source(sqlite3 *db, const char *zDb,
                       int (*xKey)(void *pArg, unsigned char *aKey, int *pnKey), void *pArg){
  void *a[2];
  a[0] = (void*)xKey;
  a[1] = pArg;
  return sqlite3_file_control(db, zDb, WS_FCNTL_SRC, (void*)a);
}
