#pragma leco tool
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
  FILE * f = fopen("model.safetensors", "rb");
  assert(f);

  uint64_t hdr_sz;
  assert(fread(&hdr_sz, sizeof(uint64_t), 1, f));

  char * json = malloc(hdr_sz + 1);
  json[hdr_sz] = 0;
  assert(fread(json, hdr_sz, 1, f));

  printf("%s\n", json);

  return 0;
}
