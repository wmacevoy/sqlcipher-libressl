#!/bin/sh
# walseal's native checks (REQ1/DUR-2, REQ6/ENC-3), against this repository's
# amalgamation and LibreSSL's libcrypto.a:
#   io     sealed WAL I/O is identical to stock
#   leak   a sealed WAL shows no header field in the clear
#   crash  every cut, torn write and reordering since the last sync recovers
#          to a commit boundary, stock and sealed (SEEDS, default "7 11 23",
#          as in the source; about two minutes per seed and mode)
#   codec  walseal keyed from SQLCipher's derived key, as the WASM worker opens
#
# Needs sqlite3.c (./configure ... && make sqlite3.c, see CONTRIBUTING.md).
#   LIBRESSL=$HOME/libressl tests/walseal/walseal.sh [io|leak|crash|codec|all]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
LIBRESSL=${LIBRESSL:-$HOME/libressl}
CC=${CC:-cc}
[ -f "$root/sqlite3.c" ] || { echo "no sqlite3.c: run make sqlite3.c first" >&2; exit 2; }
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
FLAGS="-O2 -DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL -DSQLITE_TEMP_STORE=2
  -DSQLITE_EXTRA_INIT=sqlcipher_extra_init -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown
  -I$root -I$root/wasm -I$LIBRESSL/include"
LIBS="$LIBRESSL/lib/libcrypto.a -lpthread -lm"
what=${1:-all}

case $what in io|leak|crash|all)
  # shellcheck disable=SC2086
  $CC $FLAGS -o "$out/walseal_test" "$here/walseal_test.c" "$root/wasm/walseal.c" "$root/sqlite3.c" $LIBS ;;
esac
case $what in codec|all)
  # shellcheck disable=SC2086
  $CC $FLAGS -o "$out/walseal_codec_test" "$here/walseal_codec_test.c" "$root/wasm/walseal.c" $LIBS ;;
esac

case $what in io|all) "$out/walseal_test" io "$out" 1000 16 20 ;; esac
case $what in leak|all) "$out/walseal_test" leak "$out" ;; esac
# The salt goes in the key: a torn page 1 is not walseal's to survive (DESIGN, Known hazards).
case $what in crash|all)
  for seed in ${SEEDS:-7 11 23}; do
    for m in stock sealed; do SALT_IN_KEY=1 "$out/walseal_test" crash "$out" $m $seed; done
  done ;;
esac
case $what in codec|all) "$out/walseal_codec_test" "$out" ;; esac
echo "walseal: all checks passed"
