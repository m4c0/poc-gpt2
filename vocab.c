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
  for (unsigned c = 161; c <= 172; c++) b2mb[c] = c;
  for (unsigned c = 174; c <= 255; c++) b2mb[c] = c;

  wchar_t mc = 256;
  for (unsigned c = 0; c <= 255; c++) if (!b2mb[c]) b2mb[c] = mc++;
}
static wchar_t * encode_bytes(const char * b, unsigned bytes) {
  wchar_t * mb = malloc(sizeof(wchar_t) * bytes);
  for (int i = 0; i < bytes; i++) mb[i] = b2mb[(unsigned)b[i]];
  return mb;
}

static unsigned next_token(const char * b) {
  if (!*b) return 0;
  if (*b == '\'') {
    switch (b[1]) {
      case 'l':
        if (b[2] == 'l') return 3;
        break;
      case 'r':
      case 'v':
        if (b[2] == 'e') return 3;
        break;
      case 's':
      case 't':
      case 'm':
      case 'd':
        return 2;
    }
  }

  const char * bs = *b == ' ' ? b + 1 : b;
  if (isalpha(*bs))
    while (*bs && isalpha(*bs)) bs++;
  else if (isdigit(*bs))
    while (*bs && isdigit(*bs)) bs++;
  else if (!isspace(*bs))
    while (*bs && !isalpha(*bs) && !isdigit(*bs) && !isspace(*bs)) bs++;
  else
    while (*bs && isspace(*bs)) bs++;

  return bs - b;
}

typedef struct pair {
  const char * l;
  const char * r;
} pair_t;
static pair_t bpes[50000] = {0};
void init_bpe() {
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

  pair_t * ptr = bpes;
  char * buf = bpe;
  char * nxt;
  while ((nxt = strchr(buf, '\n'))) {
    *nxt = 0;

    char * spc = strchr(buf, ' ');
    assert(spc);
    *spc = 0;

    ptr->l = buf;
    ptr->r = spc + 1;
    buf = nxt + 1;
  }
}

int main() {
  init_b2mb();
  assert(b2mb[0] == 256);
  assert(b2mb[33] == 33);
  assert(b2mb[173] == 323);

  init_bpe();

  wchar_t * mb = encode_bytes(text, strlen(text));
  assert(mb[0] == 'T');
  assert(mb[3] == 288);

  const char * txt = text;
  unsigned len;
  while ((len = next_token(txt))) {
    wchar_t * token = encode_bytes(txt, len);
     if (!token[1]) {} //add(token);
    txt += len;
  }

}
