#pragma leco tool
#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define unreachable(...) do { fprintf(stderr, __VA_ARGS__); exit(1); } while (0)

//{{{ [utl] Utilities
//====================

typedef struct utl_wstr {
  const wchar_t * str;
  int sz;
} utl_wstr_t;
static utl_wstr_t utl_wstr_new(const wchar_t * str, int sz) {
  return (utl_wstr_t) { str, sz };
}

static wchar_t * utl_wstr_printable(utl_wstr_t str) {
  wchar_t * dup = malloc((str.sz + 1) * sizeof(wchar_t));
  for (int i = 0; i < str.sz; i++) dup[i] = str.str[i] < 0x80 ? str.str[i] : '?';
  dup[str.sz] = 0;
  return dup;
}

static char * utl_slurp(const char * file) {
  FILE * f = fopen(file, "rb");
  assert(f);

  assert(0 == fseek(f, 0, SEEK_END));
  long sz = ftell(f);
  assert(sz);
  assert(0 == fseek(f, 0, SEEK_SET));

  char * data = malloc(sz + 1);
  assert(1 == fread(data, sz, 1, f));
  data[sz] = 0;

  fclose(f);
  return data;
}

//}}}

//{{{ [byt] Maps UTF-8 bytes to vocab's wchars
//=============================================

/// Maps bytes of UTF-8 into multibyte chars used in vocab.bpe.
/// Oddly enough, multibyte UTF-8 will be mapped as multiple wchars.
static wchar_t byt_map[256] = {0};

static wchar_t * byt_encode_bytes(const char * b, unsigned bytes) {
  wchar_t * mb = malloc(sizeof(wchar_t) * bytes);
  for (int i = 0; i < bytes; i++) mb[i] = byt_map[(unsigned)b[i]];
  return mb;
}
static void byt_init() {
  for (unsigned c = '!'; c <= '~'; c++) byt_map[c] = c;
  for (unsigned c = 161; c <= 172; c++) byt_map[c] = c;
  for (unsigned c = 174; c <= 255; c++) byt_map[c] = c;

  wchar_t mc = 256;
  for (unsigned c = 0; c <= 255; c++) if (!byt_map[c]) byt_map[c] = mc++;

  assert(byt_map[0] == 256);
  assert(byt_map[33] == 33);
  assert(byt_map[173] == 323);

  wchar_t * mb = byt_encode_bytes("The quick", 9);
  assert(mb[0] == 'T');
  assert(mb[3] == 288);
}

//}}}

//{{{ [bpe] Byte-pair encoding map
//=================================

static utl_wstr_t bpe_map[50000] = {0};
static wchar_t * bpe_mbstowcs(const char * u8, wchar_t * mb) {
  // This is equivalent to mbstowcs but we don't need to rely on
  // changing/restoring the multibyte locale
  for (; *u8; u8++, mb++) {
    if ((u8[0] & 0x80) == 0) {
      *mb = *u8;
      continue;
    }
    assert((u8[0] & 0xE0) == 0xC0 && (u8[1] & 0xC0) == 0x80 && "found unsupported char in vocab.bpe");
    *mb = ((u8[0] & 0x1F) << 6) | (u8[1] & 0x3F);
    u8++;
  }
  return mb;
}
static wchar_t * bpe_utf8_to_wchar(const char * a, const char * b) {
  // Final string will never be greater than original. Since it can be smaller,
  // we have to clear everything.
  wchar_t * res = calloc(strlen(a) + strlen(b) + 1, sizeof(wchar_t));
  // We concatenate both because the algo here only uses vocab.bpe for ranking.
  bpe_mbstowcs(b, bpe_mbstowcs(a, res));
  return res;
}
static void bpe_init() {
  // vocab.bpe "encodes" a list of "byte pairs", one pair for line, each pair
  // split by space. Each side of the pair is encoded as UTF-8 in the file, but
  // we should use wchar because it aligns with the tokenisation stuff.
  char * bpe = utl_slurp("vocab.bpe");

  // Skip comment in the first line
  assert(bpe[0] == '#');
  assert(bpe = strchr(bpe, '\n') + 1);

  utl_wstr_t * ptr = bpe_map;
  char * buf = bpe;
  char * nxt;
  while ((nxt = strchr(buf, '\n'))) {
    *nxt = 0;

    char * spc = strchr(buf, ' ');
    assert(spc);
    *spc = 0;

    ptr->str = bpe_utf8_to_wchar(buf, spc + 1);
    ptr->sz = wcslen(ptr->str);

    ptr++;
    buf = nxt + 1;
  }

  assert(0 == wcscmp(bpe_map[6].str, L"\x120the"));
  assert(bpe_map[6].sz == 4);
  assert(0 == wcscmp(bpe_map[6969].str, L"%."));
  assert(bpe_map[6969].sz == 2);
  assert(0 == wcscmp(bpe_map[49999].str, L"\x120gazed"));
  assert(bpe_map[49999].sz == 6);
}

typedef struct bpe_list {
  utl_wstr_t * list;
  int sz;
} bpe_list_t;
static bpe_list_t bpe_split(const wchar_t * txt, int len) {
  utl_wstr_t * list = malloc(sizeof(utl_wstr_t) * len);
  int lsz = len;
  for (int i = 0; i < lsz; i++) list[i] = utl_wstr_new(txt + i, 1);

  while (lsz > 1) {
    utl_wstr_t best = {0};
    for (int n = 0; n < 50000; n++) {
      utl_wstr_t ns = bpe_map[n];
      for (int i = 0; i < lsz - 1; i++) {
        const wchar_t * t = list[i].str;
        int tsz = list[i].sz + list[i + 1].sz;
        if (tsz != ns.sz) continue;
        if (0 != wcsncmp(t, ns.str, ns.sz)) continue;
        best = ns;
        n = 50000;
        break;
      }
    }
    // Can't compact more
    if (best.sz == 0) break;

    int wr = 0;
    for (int i = 0; i < lsz - 1; i++) {
      const wchar_t * t = list[i].str;
      int tsz = list[i].sz + list[i + 1].sz;
      if (tsz == best.sz && 0 == wcsncmp(t, best.str, best.sz)) {
        list[wr++] = utl_wstr_new(t, best.sz);
        list[i + 1].sz = 0;
        i++;
      } else {
        list[wr++] = list[i];
      }
    }
    if (list[lsz - 1].sz) list[wr++] = list[lsz - 1];
    lsz = wr;
  }

  return (bpe_list_t) { list, lsz };
}
//}}}

//{{{ [enc] Encodes wstr to token ids
//====================================

static utl_wstr_t enc_map[50257];
static void enc_init() {
  char * buf = utl_slurp("encoder.json");

  char * ptr = buf;

  char delim='{';
  while (*ptr) {
    assert(*ptr++ == delim);

    assert(*ptr++ == '"');

    int ksz = 0;
    for (char * p = ptr; *p && *p != '"'; p++, ksz++) {
      if (*p == '\\') p++;
    }
    wchar_t * key = calloc(ksz, sizeof(wchar_t));

    wchar_t * k = key;
    while (*ptr && *ptr != '"') {
      if (*ptr == '\\') {
        ptr++;
        if (*ptr == 'u') {
          ptr++;
          ksz -= 4;
          for (int i = 0; i < 4; i++, ptr++) {
            *k = *k << 4;
            if (*ptr >= '0' && *ptr <= '9') *k += *ptr - '0';
            else if (*ptr >= 'a' && *ptr <= 'f') *k += *ptr - 'a' + 10;
            else if (*ptr >= 'A' && *ptr <= 'F') *k += *ptr - 'A' + 10;
            else assert(0);
          }
          k++;
          continue;
        }
      }
      *k++ = *ptr++;
    }
    *k = 0;

    assert(*ptr++ == '"');
    assert(*ptr++ == ':');
    assert(*ptr++ == ' ');

    int id = -1;
    while (*ptr >= '0' && *ptr <= '9') {
      if (id == -1) id = 0;
      id = id * 10 + (*ptr++ - '0');
    }
    assert(id != -1);
    assert(id < 50257);

    assert(*ptr == ',' || *ptr == '}');
    ptr++;
    delim = ' ';

    enc_map[id] = utl_wstr_new(key, ksz);
  }

  free(buf);

  assert(0 == wcscmp(enc_map[236].str, L"\x130"));
  assert(0 == wcscmp(enc_map[2068].str, L"\x120quick"));
  assert(0 == wcscmp(enc_map[50256].str, L"<|endoftext|>"));
  assert(1 == enc_map[236].sz);
  assert(6 == enc_map[2068].sz);
  assert(13 == enc_map[50256].sz);
}

static int enc_find_id(utl_wstr_t str) {
  for (int tkn = 0; tkn < 50256; tkn++) {
    if (str.sz != enc_map[tkn].sz) continue;
    if (wcsncmp(str.str, enc_map[tkn].str, str.sz)) continue;
    return tkn;
  }
  unreachable("invalid token: %ls", utl_wstr_printable(str));
}

//}}}

//{{{ [tkn] Tokenisation
//=======================

static unsigned tkn_next_pptoken_len(const char * b) {
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

typedef struct tkn_ids {
  int * ids;
  int sz;
} tkn_ids_t;
static tkn_ids_t tkn_encode(const char * txt) {
  int * ids = malloc(sizeof(int) * 1024);
  int idx = 0;

  unsigned len;
  while ((len = tkn_next_pptoken_len(txt))) {
    wchar_t * token = byt_encode_bytes(txt, len);
    bpe_list_t list = bpe_split(token, len);

    for (int i = 0; i < list.sz; i++) {
      ids[idx++] = enc_find_id(list.list[i]);
    }

    txt += len;
  }

  return (tkn_ids_t) { ids, idx };
}

//}}}

int main() {
  byt_init();
  bpe_init();
  enc_init();

  const char * text = "The quick brown fox jumps over the lazy dog.";
  tkn_ids_t ts = tkn_encode(text);

  for (int i = 0; i < ts.sz; i++) printf("%d ", ts.ids[i]);
  printf("\n");

  int len = 1024;
  char * buf = calloc(len, 1);
  char * c = buf;
  for (int i = 0; i < ts.sz; i++) {
    utl_wstr_t tk = enc_map[ts.ids[i]];
    int n = wcstombs(c, utl_wstr_printable(tk), len);
    c += n;
    len -= n;
  }
  printf("%s\n", buf);
}
