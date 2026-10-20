#!/bin/sh
# The native check, the same locally and in CI: build the pinned LibreSSL,
# the release library, the smoke test, walseal's checks, then upstream's TCL
# suite.
# Exits non-zero on the first failure.
#
#   ./check.sh
#
# Environment:
#   LIBRESSL_PREFIX  where LibreSSL is installed (default $HOME/libressl)
#   CHECK_TCL        "no" skips the TCL suite (default "yes"). CI runs it on
#                    linux-glibc-x64 only, where REQ1/UP-1 promises it.
#   SEEDS            walseal's crash-replay seeds (default "7 11 23", about
#                    13 minutes; run all three before a release). CI uses "7".
#   TCL_LIBDIR       macOS: the directory holding libtcl8.6.dylib, when
#                    testfixture cannot find it (Tcl from conda or Homebrew).
#                    Not DYLD_LIBRARY_PATH: macOS drops that variable before
#                    /bin/sh starts, so it never reaches testfixture.
#
# Leaves the release library in libsqlcipher.a. The TCL suite reconfigures
# the tree with test-only flags, so after it runs, libsqlite3.a and
# sqlite3.c are gone; libsqlcipher.a is the one to ship.
set -eu

cd "$(dirname "$0")"
. ./libressl.env
prefix=${LIBRESSL_PREFIX:-$HOME/libressl}
check_tcl=${CHECK_TCL:-yes}
jobs=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

step() {
  echo
  echo "== $*"
}

sha256() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | cut -d' ' -f1
  else
    shasum -a 256 "$1" | cut -d' ' -f1
  fi
}

# A tree configured by an earlier run (perhaps with the test flags) would
# reuse its objects, so start from nothing.
if [ -f Makefile ]; then
  make distclean >/dev/null
fi

step "LibreSSL $LIBRESSL_VERSION into $prefix"
# Download to a file and check it before unpacking: through a pipe, a changed
# or truncated tarball would be built without complaint.
t=libressl-$LIBRESSL_VERSION.tar.gz
curl -fsSL -o "$work/$t" \
  "https://github.com/libressl/portable/releases/download/v$LIBRESSL_VERSION/$t"
got=$(sha256 "$work/$t")
if [ "$got" != "$LIBRESSL_SHA256" ]; then
  echo "FAIL: $t has SHA-256 $got, expected $LIBRESSL_SHA256"
  exit 1
fi
echo "$t: SHA-256 OK"
tar xzf "$work/$t" -C "$work"
mkdir "$work/libressl-build"
(
  cd "$work/libressl-build"
  cmake "$work/libressl-$LIBRESSL_VERSION" -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DLIBRESSL_APPS=OFF -DLIBRESSL_TESTS=OFF -DBUILD_SHARED_LIBS=OFF >/dev/null
  make -j"$jobs" >/dev/null
  make install >/dev/null
)

step "Release library and amalgamation"
./configure --with-tempstore=yes \
  CFLAGS="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL -DSQLITE_EXTRA_INIT=sqlcipher_extra_init -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown -I$prefix/include" \
  LDFLAGS="$prefix/lib/libcrypto.a" >/dev/null
make -j"$jobs" >/dev/null
make sqlite3.c >/dev/null
ls -l libsqlite3.a sqlite3.c sqlite3.h
# Kept under a name make distclean does not remove, before the test build.
cp libsqlite3.a libsqlcipher.a

step "REQ1/ENC-1 REQ2/ENC-2 REQ2/PLAT-1 smoke test: examples/basic.c against libsqlcipher.a"
gcc -O2 -o example_basic examples/basic.c -I. -I"$prefix/include" \
  libsqlcipher.a "$prefix/lib/libcrypto.a" -lpthread -ldl -lm
./example_basic

step "REQ1/DUR-2 REQ6/ENC-3 walseal: I/O, leak, crash replay (seeds ${SEEDS:-7 11 23}), derived key"
# Compiled only into the WASM build; checked here natively, against this
# amalgamation and LibreSSL, before the TCL suite's configure removes sqlite3.c.
LIBRESSL=$prefix sh tests/walseal/walseal.sh all

if [ "$check_tcl" = no ]; then
  step "REQ1/UP-1 REQ1/FMT-1 TCL suite: skipped (CHECK_TCL=no)"
  echo
  echo "PASS: check.sh (without the TCL suite)"
  exit 0
fi

step "REQ1/UP-1 REQ1/FMT-1 TCL suite: test/sqlcipher.test, upstream's test recipe"
# Its own configure: -DSQLCIPHER_TEST and FTS5 must never reach the shipped
# library, which is why libsqlcipher.a was copied out first.
make distclean >/dev/null
./configure --with-tempstore=yes --enable-fts5 \
  CFLAGS="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL -DSQLITE_EXTRA_INIT=sqlcipher_extra_init -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown -DSQLCIPHER_TEST -I$prefix/include" \
  LDFLAGS="$prefix/lib/libcrypto.a" >/dev/null
make -j"$jobs" testfixture >/dev/null
# The log goes to a file, not through a pipe: POSIX sh has no pipefail, and
# the status must be testfixture's (FINDINGS 2026-10-19).
if [ -n "${TCL_LIBDIR:-}" ]; then
  DYLD_LIBRARY_PATH=$TCL_LIBDIR
  export DYLD_LIBRARY_PATH
fi
status=0
(cd test && ../testfixture sqlcipher.test) >"$work/tcl.log" 2>&1 || status=$?
tail -30 "$work/tcl.log"
if [ "$status" -ne 0 ]; then
  echo "FAIL: REQ1/UP-1 testfixture sqlcipher.test exited $status"
  exit 1
fi

echo
echo "PASS: check.sh"
