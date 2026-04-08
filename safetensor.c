#pragma leco tool
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct tensor {
  int shape[4];
  uint64_t begin;
  uint64_t sz;
} tensor_t;

static char key_buf[1024];
static char shape_buf[1024];
static char offsets_buf[1024];
static tensor_t find(FILE * f, const char * key) {
  uint64_t hsz;
  assert(0 == fseek(f, 0, SEEK_SET));
  assert(fread(&hsz, 8, 1, f));

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
      .begin = atoll(offsets_buf) + hsz + 8,
      .sz = atoll(e) - atoll(offsets_buf),
    };
  }

  fprintf(stderr, "unknown key [%s]", key);
  exit(1);
}

void get_row(FILE * f, const char * tensor, int row, float * data, unsigned dsz) {
  tensor_t t = find(f, tensor);
  assert(row < t.shape[0]);
  assert(dsz == t.shape[1]);

  uint64_t rowsz = dsz * 4;
  assert(0 == fseek(f, t.begin + rowsz * row, SEEK_SET));
  assert(fread(data, rowsz, 1, f));
}

void get(FILE * f, const char * tensor, float * data, unsigned s0, unsigned s1, unsigned s2, unsigned s3) {
  tensor_t t = find(f, tensor);
  assert(s0 == t.shape[0]);
  assert(s1 == t.shape[1]);
  assert(s2 == t.shape[2]);
  assert(s3 == t.shape[3]);

  assert(0 == fseek(f, t.begin, SEEK_SET));
  assert(fread(data, t.sz, 1, f));
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

  //list(f);

  float wpe[768];
  get_row(f, "wpe.weight", 0, wpe, 768);
  float wte[768];
  get_row(f, "wte.weight", 464, wte, 768); // The

  // Embedding

  float x[768];
  for (int i = 0; i < 768; i++) x[i] = wpe[i] + wte[i];

  // Normalisation of Layer 1

  float ln1w[768];
  get(f, "h.0.ln_1.weight", ln1w, 768, 0, 0, 0);
  float ln1b[768];
  get(f, "h.0.ln_1.bias", ln1b, 768, 0, 0, 0);

  float mean = 0;
  for (int i = 0; i < 768; i++) mean += x[i];
  mean /= 768;

  float var = 0;
  for (int i = 0; i < 768; i++) var += (x[i] - mean) * (x[i] - mean);
  var /= 768;

  float y[256];
  for (int i = 0; i < 768; i++) y[i] = ln1b[i] + ln1w[i] * (x[i] - mean) / sqrtf(var + 1e-5);

  // Attention Layer 1

  // attn.c_attn contains all data for Q, followed by K, followed by V
  // Then each of QKV is split into heads (12)
  float * caw = malloc(4 * 768 * 2304);
  get(f, "h.0.attn.c_attn.weight", caw, 768, 2304, 0, 0);
  float * cab = malloc(4 * 2304);
  get(f, "h.0.attn.c_attn.bias", cab, 2304, 0, 0, 0);

  // y x caw + cab


  //for (int i = 0; i < 768; i++) printf("%9.6f\n", y[i]);

  return 0;
}
