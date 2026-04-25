#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct sft_tensor {
  int shape[4];
  uint64_t begin;
  uint64_t sz;
} sft_tensor_t;

static char sft_key_buf[1024];
static char sft_shape_buf[1024];
static char sft_offsets_buf[1024];
static sft_tensor_t sft_find(FILE * f, const char * key) {
  uint64_t hsz;
  assert(0 == fseek(f, 0, SEEK_SET));
  assert(fread(&hsz, 8, 1, f));

  fscanf(f, "{\"__metadata__\":{\"format\":\"pt\"}");
  assert(ftell(f) > 8 && "can't read safetensors file data");
  assert(!ferror(f) && !feof(f));

  char c;
  while ((c = fgetc(f)) != '}') {
    assert(c == ',');

    assert(3 == fscanf(f,
          "\"%[^\"]\":{\"dtype\":\"F32\",\"shape\":[%[^]]],\"data_offsets\":[%[^]]]}",
          sft_key_buf, sft_shape_buf, sft_offsets_buf));
    if (strcmp(key, sft_key_buf)) continue;

    char * s1 = strchr(sft_shape_buf, ',');
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

    char * e = strchr(sft_offsets_buf, ',');
    assert(e);
    *e++ = 0;

    return (sft_tensor_t) {
      .shape = {
        atoi(sft_shape_buf),
        s1 ? atoi(s1) : 0,
        s2 ? atoi(s2) : 0,
        s3 ? atoi(s3) : 0,
      },
      .begin = atoll(sft_offsets_buf) + hsz + 8,
      .sz = atoll(e) - atoll(sft_offsets_buf),
    };
  }

  fprintf(stderr, "unknown key [%s]", key);
  exit(1);
}

void sft_get_row(FILE * f, const char * tensor, int row, float * data, unsigned dsz) {
  sft_tensor_t t = sft_find(f, tensor);
  assert(row < t.shape[0]);
  assert(dsz == t.shape[1]);

  uint64_t rowsz = dsz * 4;
  assert(0 == fseek(f, t.begin + rowsz * row, SEEK_SET));
  assert(fread(data, rowsz, 1, f));
}

void sft_get(FILE * f, const char * tensor, float * data, unsigned s0, unsigned s1, unsigned s2, unsigned s3) {
  sft_tensor_t t = sft_find(f, tensor);
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
    assert(3 == fscanf(f,
          "\"%[^\"]\":{\"dtype\":\"F32\",\"shape\":[%[^]]],\"data_offsets\":[%[^]]]}",
          sft_key_buf, sft_shape_buf, sft_offsets_buf));
    printf("%s %s %s\n", sft_key_buf, sft_shape_buf, sft_offsets_buf);
  }
}

int main() {
  FILE * f = fopen("model.safetensors", "rb");
  assert(f);

  list(f);

  float wpe[768];
  sft_get_row(f, "wpe.weight", 0, wpe, 768);
  float wte[768];
  sft_get_row(f, "wte.weight", 464, wte, 768); // The

  // Embedding

  float x[768];
  for (int i = 0; i < 768; i++) x[i] = wpe[i] + wte[i];

  // Normalisation of Layer 1

  float ln1w[768];
  sft_get(f, "h.0.ln_1.weight", ln1w, 768, 0, 0, 0);
  float ln1b[768];
  sft_get(f, "h.0.ln_1.bias", ln1b, 768, 0, 0, 0);

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
  sft_get(f, "h.0.attn.c_attn.weight", caw, 768, 2304, 0, 0);
  float * cab = malloc(4 * 2304);
  sft_get(f, "h.0.attn.c_attn.bias", cab, 2304, 0, 0, 0);

  // y x caw + cab


  //for (int i = 0; i < 768; i++) printf("%9.6f\n", y[i]);

  return 0;
}
