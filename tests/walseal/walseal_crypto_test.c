/*
** walseal_crypto_test.c -- REQ6/ENC-3: walseal's crypto backend computes
** AES-256 and HMAC-SHA256, checked against published vectors.
**
** walseal's other checks cannot see a wrong block cipher: the Feistel
** network XORs in its tweaks, so even an identity "AES" leaves no header
** field in the clear, while anyone could undo it.  Every backend
** (walseal_crypto_libressl.c here, walseal_crypto_psa.c in
** sqlcipher-embedded) must pass this, and so seal identically.
**
**   walseal_crypto_test       exits 0 and prints "walseal crypto: ok"
*/
#include <stdio.h>
#include <string.h>
#include "walseal_crypto.h"

static int hex(const char *h, unsigned char *out){
  int n = 0;
  unsigned v;
  while(h[0] && h[1] && sscanf(h, "%2x", &v) == 1){ out[n++] = (unsigned char)v; h += 2; }
  return n;
}

static int check(const char *what, const unsigned char *got, const char *wantHex){
  unsigned char want[64];
  int n = hex(wantHex, want);
  if(memcmp(got, want, (size_t)n) != 0){
    printf("FAIL: REQ6/ENC-3 walseal crypto: %s\n", what);
    return 1;
  }
  return 0;
}

int main(void){
  unsigned char key[32], in[32], out[32], mac[32];
  walseal_aes *aes = 0;
  int bad = 0;

  /* FIPS-197 appendix C.3, twice: two blocks in one call, each on its own. */
  hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key);
  hex("00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff", in);
  if(walseal_aes_new(&aes, key) != SQLITE_OK || walseal_aes_encrypt(aes, in, out, 32) != SQLITE_OK){
    printf("FAIL: REQ6/ENC-3 walseal crypto: AES-256 setup or encrypt failed\n");
    return 1;
  }
  bad |= check("AES-256 FIPS-197 C.3", out,
               "8ea2b7ca516745bfeafc49904b4960898ea2b7ca516745bfeafc49904b496089");
  /* In place, as walseal calls it. */
  memcpy(out, in, 16);
  if(walseal_aes_encrypt(aes, out, out, 16) != SQLITE_OK) bad = 1;
  bad |= check("AES-256 FIPS-197 C.3, in place", out, "8ea2b7ca516745bfeafc49904b496089");
  walseal_aes_free(aes);

  /* RFC 4231 test case 1. */
  memset(key, 0x0b, 20);
  if(walseal_hmac_sha256(key, 20, (const unsigned char*)"Hi There", 8, mac) != SQLITE_OK) bad = 1;
  bad |= check("HMAC-SHA256 RFC 4231 case 1", mac,
               "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");

  memset(mac, 0xff, sizeof(mac));
  walseal_cleanse(mac, sizeof(mac));
  for(int i = 0; i < (int)sizeof(mac); i++) if(mac[i]) bad |= check("cleanse", mac, "00");

  if(!bad) printf("walseal crypto: ok\n");
  return bad;
}
