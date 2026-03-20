# sqlcipher-libressl

SQLCipher fork patched for LibreSSL compatibility.

## Patches (2 changes over SQLCipher v4.14.0)

### 1. HMAC: EVP_MAC → legacy HMAC API (`crypto_openssl.c`)

LibreSSL doesn't have `EVP_MAC` (OpenSSL 3 API). Replaced with
the legacy `HMAC_CTX_new`/`HMAC_Init_ex`/`HMAC_Update`/`HMAC_Final`
API which works in both LibreSSL and all OpenSSL versions.

### 2. WASM: atexit instead of .fini_array (`sqlcipher.c`)

Emscripten/WASM doesn't support `.fini_array` sections. Added
`__EMSCRIPTEN__` guard: uses `atexit(sqlcipher_fini)` for WASM,
original `.fini_array` for native Linux. Key zeroing behavior
preserved on both platforms.

## Apply as patch

```bash
cd vendor/sqlcipher  # upstream SQLCipher v4.14.0
git apply ../sqlcipher-libressl.patch
```

## Why

SQLCipher + LibreSSL + WASM = encrypted SQLite in the browser
with a lighter crypto library than OpenSSL. Used by the y8
project for encryption at rest in browser examples.

## License

BSD (same as upstream SQLCipher)
