#pragma leco tool
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct tensor {
  int shape[4];
  long begin;
  long end;
} tensor_t;

static char key_buf[1024];
static char shape_buf[1024];
static char offsets_buf[1024];
static tensor_t find(FILE * f, const char * key) {
  assert(0 == fseek(f, 8, SEEK_SET));

  fscanf(f, "{\"__metadata__\":{\"format\":\"pt\"}");
  assert(!ferror(f) && !feof(f));

  char c;
  while ((c = fgetc(f)) != '}') {
    assert(c == ',');

    assert(3 == fscanf(f,
          "\"%[^\"]\":{\"dtype\":\"F32\",\"shape\":[%[^]]],\"data_offsets\":[%[^]]]}",
          key_buf, shape_buf, offsets_buf));
    if (strcmp(key, key_buf)) continue;

    char * s1 = strchr(shape_buf, ',');
    char * s2 = 0;
    char * s3 = 0;
    if (s1) {
      *s1++ = 0;
      s2 = strchr(s1, ',');
      if (s2) {
        *s2++ = 0;
        s3 = strchr(s2, ',');
        if (s3) *s3++ = 0;
      }
    }

    char * e = strchr(offsets_buf, ',');
    assert(e);
    *e++ = 0;

    return (tensor_t) {
      .shape = {
        atoi(shape_buf),
        s1 ? atoi(s1) : 0,
        s2 ? atoi(s2) : 0,
        s3 ? atoi(s3) : 0,
      },
      .begin = atol(offsets_buf),
      .end = atol(e),
    };
  }

  fprintf(stderr, "unknown key [%s]", key);
  exit(1);
}

void list(FILE * f) {
  assert(0 == fseek(f, 8, SEEK_SET));
  fscanf(f, "{\"__metadata__\":{\"format\":\"pt\"}");
  assert(!ferror(f) && !feof(f));

  char c;
  while ((c = fgetc(f)) != '}') {
    assert(1 == fscanf(f,
          "\"%[^\"]\":{\"dtype\":\"F32\",\"shape\":[%*[^]]],\"data_offsets\":[%*[^]]]}",
          key_buf));
    puts(key_buf);
  }
}

int main() {
  FILE * f = fopen("model.safetensors", "rb");
  assert(f);

  // list(f);

  tensor_t wpe = find(f, "wpe.weight");
  tensor_t wte = find(f, "wte.weight");
  
  printf("wpe: %d,%d,%d,%d -- %ld %ld -- %ld\n",
      wpe.shape[0], wpe.shape[1], wpe.shape[2], wpe.shape[3],
      wpe.begin, wpe.end, wpe.end - wpe.begin);
  printf("wte: %d,%d,%d,%d -- %ld %ld -- %ld\n",
      wte.shape[0], wte.shape[1], wte.shape[2], wte.shape[3],
      wte.begin, wte.end, wte.end - wte.begin);

  return 0;
}
