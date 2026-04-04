#pragma leco tool
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

// The objective is to create valid GPT-2 tokens for this message
static const char * text = "The quick brown fox jumps over the lazy fox.";

/// Transforms bytes containing UTF-8 into the multibyte chars used in vocab.bpe
static wchar_t b2mb[256] = {0};
static void init_b2mb() {
  for (unsigned c = '!'; c <= '~'; c++) b2mb[c] = c;
  for (unsigned c = 161; c <= 255; c++) b2mb[c] = c;

  wchar_t mc = 256;
  for (unsigned c = 0; c <= 255; c++) if (!b2mb[c]) b2mb[c] = mc++;
}
wchar_t * encode_bytes(const char * b, unsigned bytes) {
  wchar_t * mb = malloc(sizeof(wchar_t) * bytes);
  for (int i = 0; i < bytes; i++) mb[i] = b2mb[(unsigned)b[i]];
  return mb;
}

int main() {
  init_b2mb();

  wchar_t * mb = encode_bytes(text, strlen(text));
  printf("S: %s\n", text);
  wprintf(L"L: %ls\n", mb);

  FILE * f = fopen("vocab.bpe", "rb");
  assert(f);

  assert(0 == fseek(f, 0, SEEK_END));
  long sz = ftell(f);
  assert(sz);
  assert(0 == fseek(f, 0, SEEK_SET));

  char * bpe = malloc(sz + 1);
  assert(1 == fread(bpe, sz, 1, f));

  // Skip comment in the first line
  assert(bpe[0] == '#');
  assert(bpe = strchr(bpe, '\n') + 1);

  char * buf = bpe;
  char * nxt;
  while ((nxt = strchr(buf, '\n'))) {
    *nxt = 0;

    char * spc = strchr(buf, ' ');
    assert(spc);
    *spc = 0;

    fprintf(stderr, "[%s][%s] ", buf, spc + 1);
    buf = nxt + 1;
  }
}
