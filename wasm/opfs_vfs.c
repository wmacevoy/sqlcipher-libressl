/* ============================================================
 * opfs_vfs.c — SQLite VFS backed by OPFS (Origin Private File System)
 *
 * Every xRead/xWrite/xSync maps directly to OPFS
 * FileSystemSyncAccessHandle methods via EM_JS callbacks.
 * Runs in a Web Worker. No SharedArrayBuffer needed.
 *
 * Register at startup:
 *   sqlite3_opfs_init();  // makes "opfs" the default VFS
 *
 * Then open normally:
 *   sqlite3_open("/mydb.db", &db);
 *   // writes go to OPFS, durable on every COMMIT
 * ============================================================ */

#include <string.h>
#include <emscripten.h>
#include "sqlite3.h"

/* ── File handle ──────────────────────────────────────────── */

typedef struct OpfsFile {
    sqlite3_file base;
    int hid;          /* JS handle table index */
} OpfsFile;

/* ── EM_JS: C calls into JS OPFS handles ─────────────────── */

/* xOpen: look up a pre-opened handle by filename.
   Handles are pre-opened asynchronously in JS before sqlite3_open(). */
EM_JS(int, js_opfs_open, (const char *zName, int nameLen, int flags), {
    var name = UTF8ToString(zName, nameLen);
    var pre = Module._opfs_preopen;
    if (pre && pre[name] !== undefined) {
        return pre[name];
    }
    /* Journal/WAL files: open synchronously if handle exists, or return -1 */
    if (Module._opfs_open_sync) {
        return Module._opfs_open_sync(name, flags);
    }
    return -1;
});

EM_JS(void, js_opfs_close, (int hid), {
    Module._opfs_close(hid);
});

EM_JS(int, js_opfs_read, (int hid, int pDest, int n, double offset), {
    return Module._opfs_read(hid, pDest, n, offset);
});

EM_JS(int, js_opfs_write, (int hid, int pSrc, int n, double offset), {
    return Module._opfs_write(hid, pSrc, n, offset);
});

EM_JS(int, js_opfs_sync, (int hid), {
    return Module._opfs_sync(hid);
});

EM_JS(double, js_opfs_filesize, (int hid), {
    return Module._opfs_filesize(hid);
});

EM_JS(int, js_opfs_truncate, (int hid, double size), {
    return Module._opfs_truncate(hid, size);
});

EM_JS(int, js_opfs_delete, (const char *zName, int nameLen), {
    var name = UTF8ToString(zName, nameLen);
    return Module._opfs_delete(name);
});

EM_JS(int, js_opfs_access, (const char *zName, int nameLen), {
    var name = UTF8ToString(zName, nameLen);
    return Module._opfs_access(name);
});

/* ── io_methods ───────────────────────────────────────────── */

static int opfsClose(sqlite3_file *pFile) {
    OpfsFile *f = (OpfsFile *)pFile;
    js_opfs_close(f->hid);
    f->hid = -1;
    return SQLITE_OK;
}

static int opfsRead(sqlite3_file *pFile, void *zBuf, int iAmt, sqlite3_int64 iOfst) {
    OpfsFile *f = (OpfsFile *)pFile;
    return js_opfs_read(f->hid, (int)zBuf, iAmt, (double)iOfst);
}

static int opfsWrite(sqlite3_file *pFile, const void *zBuf, int iAmt, sqlite3_int64 iOfst) {
    OpfsFile *f = (OpfsFile *)pFile;
    return js_opfs_write(f->hid, (int)zBuf, iAmt, (double)iOfst);
}

static int opfsTruncate(sqlite3_file *pFile, sqlite3_int64 size) {
    OpfsFile *f = (OpfsFile *)pFile;
    return js_opfs_truncate(f->hid, (double)size);
}

static int opfsSync(sqlite3_file *pFile, int flags) {
    OpfsFile *f = (OpfsFile *)pFile;
    (void)flags;
    return js_opfs_sync(f->hid);
}

static int opfsFileSize(sqlite3_file *pFile, sqlite3_int64 *pSize) {
    OpfsFile *f = (OpfsFile *)pFile;
    *pSize = (sqlite3_int64)js_opfs_filesize(f->hid);
    return SQLITE_OK;
}

/* Locking: no-op. OPFS createSyncAccessHandle provides exclusion. */
static int opfsLock(sqlite3_file *p, int l)    { (void)p; (void)l; return SQLITE_OK; }
static int opfsUnlock(sqlite3_file *p, int l)  { (void)p; (void)l; return SQLITE_OK; }
static int opfsCheckLock(sqlite3_file *p, int *r) { (void)p; *r = 0; return SQLITE_OK; }

static int opfsFileControl(sqlite3_file *p, int op, void *a) {
    (void)p; (void)op; (void)a;
    return SQLITE_NOTFOUND;
}

static int opfsSectorSize(sqlite3_file *p) { (void)p; return 4096; }
static int opfsDevChar(sqlite3_file *p)    { (void)p; return 0; }

static sqlite3_io_methods opfs_io_methods = {
    1,                    /* iVersion */
    opfsClose,
    opfsRead,
    opfsWrite,
    opfsTruncate,
    opfsSync,
    opfsFileSize,
    opfsLock,
    opfsUnlock,
    opfsCheckLock,
    opfsFileControl,
    opfsSectorSize,
    opfsDevChar
};

/* ── VFS methods ──────────────────────────────────────────── */

static int opfsOpen(sqlite3_vfs *pVfs, const char *zName, sqlite3_file *pFile,
                    int flags, int *pOutFlags) {
    (void)pVfs;
    OpfsFile *f = (OpfsFile *)pFile;
    f->base.pMethods = NULL;

    int hid = js_opfs_open(zName, zName ? (int)strlen(zName) : 0, flags);
    if (hid < 0) return SQLITE_CANTOPEN;

    f->hid = hid;
    f->base.pMethods = &opfs_io_methods;
    if (pOutFlags) *pOutFlags = flags;
    return SQLITE_OK;
}

static int opfsDelete(sqlite3_vfs *pVfs, const char *zName, int syncDir) {
    (void)pVfs; (void)syncDir;
    return js_opfs_delete(zName, (int)strlen(zName));
}

static int opfsAccess(sqlite3_vfs *pVfs, const char *zName, int flags, int *pResOut) {
    (void)pVfs; (void)flags;
    *pResOut = js_opfs_access(zName, (int)strlen(zName));
    return SQLITE_OK;
}

static int opfsFullPathname(sqlite3_vfs *pVfs, const char *zName, int nOut, char *zOut) {
    (void)pVfs;
    int n = (int)strlen(zName);
    if (n >= nOut) n = nOut - 1;
    memcpy(zOut, zName, n);
    zOut[n] = 0;
    return SQLITE_OK;
}

static int opfsRandomness(sqlite3_vfs *p, int n, char *z) {
    (void)p;
    /* Emscripten provides getentropy via crypto.getRandomValues */
    extern int getentropy(void *, size_t);
    getentropy(z, n);
    return n;
}

static int opfsSleep(sqlite3_vfs *p, int us) { (void)p; (void)us; return 0; }

static int opfsCurrentTime(sqlite3_vfs *p, double *pTime) {
    (void)p;
    *pTime = emscripten_get_now() / 86400000.0 + 2440587.5;
    return SQLITE_OK;
}

static int opfsGetLastError(sqlite3_vfs *p, int n, char *z) {
    (void)p; (void)n; (void)z;
    return 0;
}

static sqlite3_vfs opfs_vfs = {
    1,                    /* iVersion */
    sizeof(OpfsFile),     /* szOsFile */
    512,                  /* mxPathname */
    0,                    /* pNext */
    "opfs",               /* zName */
    0,                    /* pAppData */
    opfsOpen,
    opfsDelete,
    opfsAccess,
    opfsFullPathname,
    0,                    /* xDlOpen */
    0,                    /* xDlError */
    0,                    /* xDlSym */
    0,                    /* xDlClose */
    opfsRandomness,
    opfsSleep,
    opfsCurrentTime,
    opfsGetLastError
};

/* ── Register ─────────────────────────────────────────────── */

int sqlite3_opfs_init(void) {
    return sqlite3_vfs_register(&opfs_vfs, 1);  /* 1 = make default */
}
