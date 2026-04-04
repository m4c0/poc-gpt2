#pragma leco tool
#include <assert.h>
#include <stdio.h>
#include <string.h>

// The objective is to create valid GPT-2 tokens for this message
static const char * text = "The quick brown fox jumps over the lazy fox.";

int main() {

  FILE * f = fopen("vocab.bpe", "rb");
  assert(f);

  char buf[1024];
  assert(fgets(buf, sizeof(buf), f));
  assert(buf[0] == '#');

  while (fgets(buf, sizeof(buf), f)) {
    buf[strlen(buf) - 1] = 0;

    char * spc = strchr(buf, ' ');
    assert(spc);
    *spc = 0;

    fprintf(stderr, "[%s][%s] ", buf, spc + 1);
  }
}
