#pragma leco tool
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
  FILE * f = fopen("model.safetensors", "rb");
  assert(f);

  uint64_t hdr_sz;
  assert(fread(&hdr_sz, sizeof(uint64_t), 1, f));

  fscanf(f, "{\"__metadata__\":{\"format\":\"pt\"}");
  assert(!ferror(f) && !feof(f));

  char * key = malloc(1024);
  char * shape = malloc(1024);
  char * offsets = malloc(1024);

  char c;
  while ((c = fgetc(f)) != '}') {
    assert(c == ',');

    assert(3 == fscanf(f,
          "\"%[^\"]\":{\"dtype\":\"F32\",\"shape\":[%[^]]],\"data_offsets\":[%[^]]]}",
          key, shape, offsets));

    printf("%s %s %s\n", key, shape, offsets);
  }

  //printf("%s\n", json);

  return 0;
}
