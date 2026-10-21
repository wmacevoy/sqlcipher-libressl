/*
** walseal_crypto_libressl.c -- walseal_crypto.h over LibreSSL's libcrypto:
** EVP AES-256-ECB without padding, and HMAC().
*/
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/crypto.h>
#include "walseal_crypto.h"

struct walseal_aes { EVP_CIPHER_CTX *ctx; };

int walseal_aes_new(walseal_aes **pp, const unsigned char *key){
  walseal_aes *p = sqlite3_malloc(sizeof(*p));
  *pp = 0;
  if(!p) return SQLITE_ERROR;
  p->ctx = EVP_CIPHER_CTX_new();
  if(!p->ctx
     || EVP_EncryptInit_ex(p->ctx, EVP_aes_256_ecb(), 0, key, 0) != 1
     || EVP_CIPHER_CTX_set_padding(p->ctx, 0) != 1){
    walseal_aes_free(p);
    return SQLITE_ERROR;
  }
  *pp = p;
  return SQLITE_OK;
}

/* ECB without padding keeps no state between calls, so one context serves
** every call. */
int walseal_aes_encrypt(walseal_aes *p, const unsigned char *in, unsigned char *out, size_t n){
  int olen = 0;
  return EVP_EncryptUpdate(p->ctx, out, &olen, in, (int)n) == 1 && olen == (int)n
         ? SQLITE_OK : SQLITE_ERROR;
}

void walseal_aes_free(walseal_aes *p){
  if(!p) return;
  EVP_CIPHER_CTX_free(p->ctx);    /* wipes the key schedule */
  sqlite3_free(p);
}

int walseal_hmac_sha256(const unsigned char *key, int nKey,
                        const unsigned char *msg, int nMsg, unsigned char *out){
  unsigned int len = 0;
  return HMAC(EVP_sha256(), key, nKey, msg, (size_t)nMsg, out, &len) && len == 32
         ? SQLITE_OK : SQLITE_ERROR;
}

void walseal_cleanse(void *p, size_t n){
  OPENSSL_cleanse(p, n);
}
