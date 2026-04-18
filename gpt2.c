#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define VOLK_IMPLEMENTATION
#include "volk.h"

#include "Vulkan-Headers/include/vulkan/vulkan_core.h"

#define unreachable(...) do { fprintf(stderr, __VA_ARGS__); exit(1); } while (0)

const char * text = "What's the capital of France?";

// TODO: add temperature
// TODO: add penalty for repeating tokens
// TODO: add KV-cache
// TODO: print tokens inside loop and measure performance

// Note: temperature is about
// 1. Divide logits (ie x0) by a number between 1 and 0 (when close to "0",
//    it is the same as "argmax"
// 2. Softmax result of "1" to create a percentage that adds to 1.0
// 3. Pick a random number between 0 and 1 and check it against "2"

// Note: penalty for repeating tokens is a matter of subtractring weights of
// logits for tokens that were previously used

// Note: KV-cache might improve the speed but it might also nuke the clarity
// of the code

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
static char byt_rev_map[65536] = {0};

static wchar_t * byt_encode_bytes(const char * b, unsigned bytes) {
  wchar_t * mb = malloc(sizeof(wchar_t) * bytes);
  for (int i = 0; i < bytes; i++) mb[i] = byt_map[(unsigned)b[i]];
  return mb;
}
static int byt_decode_bytes(utl_wstr_t str, char * dst, unsigned dsz) {
  int i;
  for (i = 0; i < str.sz && i < dsz; i++) dst[i] = byt_rev_map[str.str[i]];
  return i;
}
static void byt_init() {
  for (unsigned c = '!'; c <= '~'; c++) byt_map[c] = c;
  for (unsigned c = 161; c <= 172; c++) byt_map[c] = c;
  for (unsigned c = 174; c <= 255; c++) byt_map[c] = c;

  wchar_t mc = 256;
  for (unsigned c = 0; c <= 255; c++) if (!byt_map[c]) byt_map[c] = mc++;

  for (int i = 0; i < 256; i++) byt_rev_map[byt_map[i]] = i;

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
static int tkn_decode(tkn_ids_t ts, char * buf, int bsz) {
  int total = 0;
  for (int i = 0; i < ts.sz && i < bsz; i++) {
    utl_wstr_t tk = enc_map[ts.ids[i]];
    total += byt_decode_bytes(tk, buf + total, bsz - total);
  }
  if (total < bsz) buf[total] = 0;
  return total;
}

//}}}

//{{{ [sft] Safetensors

typedef struct sft_tensor {
  int shape[4];
  uint64_t begin;
  uint64_t sz;
} sft_tensor_t;

static FILE * sft_file;
static void sft_init() {
  sft_file = fopen("model.safetensors", "rb");
  assert(sft_file);
}

static char sft_key_buf[1024];
static char sft_shape_buf[1024];
static char sft_offsets_buf[1024];
static sft_tensor_t sft_find(const char * key) {
  uint64_t hsz;
  assert(0 == fseek(sft_file, 0, SEEK_SET));
  assert(fread(&hsz, 8, 1, sft_file));

  fscanf(sft_file, "{\"__metadata__\":{\"format\":\"pt\"}");
  assert(!ferror(sft_file) && !feof(sft_file));

  char c;
  while ((c = fgetc(sft_file)) != '}') {
    assert(c == ',');

    assert(3 == fscanf(sft_file,
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

void sft_get_row(const char * tensor, int row, float * data, unsigned dsz) {
  sft_tensor_t t = sft_find(tensor);
  assert(row < t.shape[0]);
  assert(dsz == t.shape[1]);

  uint64_t rowsz = dsz * 4;
  assert(0 == fseek(sft_file, t.begin + rowsz * row, SEEK_SET));
  assert(fread(data, rowsz, 1, sft_file));
}

void sft_get(const char * tensor, float * data, unsigned s0, unsigned s1, unsigned s2, unsigned s3) {
  sft_tensor_t t = sft_find(tensor);
  assert(s0 == t.shape[0]);
  assert(s1 == t.shape[1]);
  assert(s2 == t.shape[2]);
  assert(s3 == t.shape[3]);

  assert(0 == fseek(sft_file, t.begin, SEEK_SET));
  assert(fread(data, t.sz, 1, sft_file));
}

//}}}

//{{{ [vlk] Vulkan

static VkInstance vlk_ins;
static VkDevice vlk_dev;
static VkCommandPool vlk_cpool;
static VkDescriptorPool vlk_dpool;
static VkDescriptorSetLayout vlk_dsl;
static VkPhysicalDevice vlk_pd;
static VkQueue vlk_q;
static VkQueryPool vlk_qpool;
static unsigned vlk_qf;

static void vlk_check(VkResult r, const char * msg) {
  if (r == VK_SUCCESS) return;
  fprintf(stderr, "Vulkan call failed (code=%d): %s\n", r, msg);
  exit(1);
}
#define _(X) vlk_check((X), #X)

static void vlk_create_instance() {
  VkApplicationInfo app = {
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .apiVersion = VK_API_VERSION_1_2,
  };
  VkInstanceCreateInfo info = (VkInstanceCreateInfo) {
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pApplicationInfo = &app,
  };
#ifdef __APPLE__
  const char * ext[] = {
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
  };
  info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  info.enabledExtensionCount = 1;
  info.ppEnabledExtensionNames = ext;
#endif
  _(vkCreateInstance(&info, NULL, &vlk_ins));
  volkLoadInstance(vlk_ins);
}

static void vlk_find_physical_device() {
  VkPhysicalDevice pd[16];
  uint32_t pdsz = 16;
  _(vkEnumeratePhysicalDevices(vlk_ins, &pdsz, pd));
  for (int i = 0; i < pdsz; i++) {
    VkQueueFamilyProperties qp[16];
    uint32_t qpsz = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(pd[i], &qpsz, qp);
    for (vlk_qf = 0; vlk_qf < qpsz; vlk_qf++) {
      if ((qp[vlk_qf].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0) continue;
      if ((qp[vlk_qf].queueFlags & VK_QUEUE_TRANSFER_BIT) == 0) continue;
      vlk_pd = pd[i];
      return;
    }
  }
  assert(0);
}

static void vlk_create_device() {
  const float pri = 1.0f;
  VkDeviceQueueCreateInfo q = (VkDeviceQueueCreateInfo) {
    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .queueCount = 1,
    .pQueuePriorities = &pri,
    .queueFamilyIndex = vlk_qf,
  };

  VkDeviceCreateInfo info = (VkDeviceCreateInfo) {
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .queueCreateInfoCount = 1,
    .pQueueCreateInfos = &q,
    .enabledExtensionCount = 0,
    .ppEnabledExtensionNames = (const char *[]) {
      "VK_KHR_portability_subset"
    },
  };
#ifdef __APPLE__
  info.enabledExtensionCount = 1;
#endif

  _(vkCreateDevice(vlk_pd, &info, NULL, &vlk_dev));
  volkLoadDevice(vlk_dev);

  vkGetDeviceQueue(vlk_dev, vlk_qf, 0, &vlk_q);
}

static VkShaderModule vlk_create_shader_module(const char * name) {
  FILE * f = fopen(name, "rb");
  assert(f);
  assert(0 == fseek(f, 0, SEEK_END));
  long sz = ftell(f);
  assert(sz && (sz % 4 == 0));
  assert(0 == fseek(f, 0, SEEK_SET));
  uint32_t * data = malloc(sz);
  assert(1 == fread(data, sz, 1, f));
  fclose(f);

  VkShaderModuleCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = sz,
    .pCode = data,
  };

  VkShaderModule mod;
  _(vkCreateShaderModule(vlk_dev, &info, NULL, &mod));

  free(data);
  return mod;
}

static void vlk_create_descriptor_set_layouts() {
  VkDescriptorSetLayoutBinding bis[] = {{
    .binding = 0,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
  }};
  VkDescriptorSetLayoutCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount = 1,
    .pBindings = bis,
  };
  _(vkCreateDescriptorSetLayout(vlk_dev, &info, NULL, &vlk_dsl));
}

static void vlk_create_descriptor_pool() {
  VkDescriptorPoolSize pszs[1] = {{
    .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1024,
  }};
  VkDescriptorPoolCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .maxSets = 1024,
    .poolSizeCount = 1,
    .pPoolSizes = pszs,
  };
  _(vkCreateDescriptorPool(vlk_dev, &info, NULL, &vlk_dpool));
}

typedef struct vlk_ppl {
  VkPipelineLayout pl;
  VkPipeline ppl;
  unsigned sets;
} vlk_ppl_t;
static vlk_ppl_t vlk_ppl_cache[128];
static unsigned vlk_ppl_cache_idx = 0;
static vlk_ppl_t vlk_create_pipeline(const char * name, unsigned set_count, unsigned pcsz) {
  assert(set_count < 8);

  vlk_ppl_t res;
  res.sets = set_count;

  VkDescriptorSetLayout dsls[8];
  for (int i = 0; i < set_count; i++) dsls[i] = vlk_dsl;
  VkPipelineLayoutCreateInfo pli = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = set_count,
    .pSetLayouts = dsls,
  };

  VkPushConstantRange pc = {
    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    .size = pcsz,
  };
  if (pcsz) {
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pc;
  }

  _(vkCreatePipelineLayout(vlk_dev, &pli, NULL, &res.pl));

  VkShaderModule mod = vlk_create_shader_module(name);

  VkComputePipelineCreateInfo infos[] = {{
    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .pName = "main",
      .module = mod,
    },
    .layout = res.pl,
  }};

  _(vkCreateComputePipelines(vlk_dev, NULL, 1, infos, NULL, &res.ppl));
  vkDestroyShaderModule(vlk_dev, mod, NULL);
  vlk_ppl_cache[vlk_ppl_cache_idx++] = res;
  return res;
}

typedef struct vlk_buffer {
  VkBuffer buf;
  VkDeviceMemory mem;
  VkDescriptorSet dset;
  unsigned size;
} vlk_buffer_t;
static vlk_buffer_t vlk_buf_cache[1024];
static unsigned vlk_buf_cache_idx = 0;
static vlk_buffer_t vlk_create_buffer(VkDeviceSize sz, VkMemoryPropertyFlags mem_flags, VkBufferUsageFlags ex_flags) {
  VkPhysicalDeviceMemoryProperties props;
  vkGetPhysicalDeviceMemoryProperties(vlk_pd, &props);

  for (int i = 0; i < props.memoryTypeCount; i++) {
    VkMemoryPropertyFlags flags = props.memoryTypes[i].propertyFlags;
    if ((flags & mem_flags) != mem_flags) continue;

    vlk_buffer_t res;
    res.size = sz * sizeof(float);

    VkBufferCreateInfo buf = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = sz * sizeof(float),
      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | ex_flags,
    };
    _(vkCreateBuffer(vlk_dev, &buf, NULL, &res.buf));

    VkMemoryAllocateInfo mem = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = sz * sizeof(float),
      .memoryTypeIndex = i,
    };
    _(vkAllocateMemory(vlk_dev, &mem, NULL, &res.mem));
    _(vkBindBufferMemory(vlk_dev, res.buf, res.mem, 0));

    VkDescriptorSetAllocateInfo ds = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = vlk_dpool,
      .descriptorSetCount = 1,
      .pSetLayouts = &vlk_dsl,
    };
    _(vkAllocateDescriptorSets(vlk_dev, &ds, &res.dset));

    VkDescriptorBufferInfo dbi = { res.buf, 0, VK_WHOLE_SIZE };
    VkWriteDescriptorSet wr[] = {{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = res.dset,
      .dstBinding = 0,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &dbi,
    }};
    vkUpdateDescriptorSets(vlk_dev, 1, wr, 0, NULL);

    vlk_buf_cache[vlk_buf_cache_idx++] = res;
    return res;
  }
  unreachable("could not find host memory with Vulkan");
}
static vlk_buffer_t vlk_create_host_buffer(VkDeviceSize sz, VkBufferUsageFlags ex_flags) {
  return vlk_create_buffer(sz,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      ex_flags);
}
static vlk_buffer_t vlk_create_local_buffer(VkDeviceSize sz, VkBufferUsageFlags ex_flags) {
  return vlk_create_buffer(sz, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ex_flags);
}

static void vlk_create_query_pool() {
  VkQueryPoolCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
    .queryType = VK_QUERY_TYPE_TIMESTAMP,
    .queryCount = 1024,
  };
  _(vkCreateQueryPool(vlk_dev, &info, NULL, &vlk_qpool));
}

static void vlk_create_command_pool() {
  VkCommandPoolCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
  };
  _(vkCreateCommandPool(vlk_dev, &info, NULL, &vlk_cpool));
}

static VkCommandBuffer vlk_allocate_command_buffer() {
  VkCommandBufferAllocateInfo info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = vlk_cpool,
    .commandBufferCount = 1,
  };
  VkCommandBuffer cb;
  _(vkAllocateCommandBuffers(vlk_dev, &info, &cb));
  return cb;
}

static void vlk_begin_command_buffer(VkCommandBuffer cb) {
  VkCommandBufferBeginInfo info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
  };
  vkBeginCommandBuffer(cb, &info);
}
static void vlk_end_command_buffer(VkCommandBuffer cb) {
  vkEndCommandBuffer(cb);
}

static void vlk_submit(VkCommandBuffer cb) {
  VkSubmitInfo info = {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pCommandBuffers = &cb,
    .commandBufferCount = 1,
  };
  _(vkQueueSubmit(vlk_q, 1, &info, NULL));
}

static void vlk_init() {
  _(volkInitialize());

  vlk_create_instance();
  vlk_find_physical_device();
  vlk_create_device();
  vlk_create_descriptor_pool();
  vlk_create_descriptor_set_layouts();
  vlk_create_command_pool();
  vlk_create_query_pool();
}
static void vlk_deinit() {
  uint64_t n = 0;
  for (int i = 0; i < vlk_buf_cache_idx; i++) n += vlk_buf_cache[i].size;
  fprintf(stderr, "\nTotal buffer size: %lluMB\n", n / (1024 * 1024));

  for (int i = 0; i < vlk_buf_cache_idx; i++) {
    vkDestroyBuffer(vlk_dev, vlk_buf_cache[i].buf, NULL);
    vkFreeMemory(vlk_dev, vlk_buf_cache[i].mem, NULL);
  }
  for (int i = 0; i < vlk_ppl_cache_idx; i++) {
    vkDestroyPipelineLayout(vlk_dev, vlk_ppl_cache[i].pl, NULL);
    vkDestroyPipeline(vlk_dev, vlk_ppl_cache[i].ppl, NULL);
  }
  vkDestroyDescriptorSetLayout(vlk_dev, vlk_dsl, NULL);
  vkDestroyDescriptorPool(vlk_dev, vlk_dpool, NULL);
  vkDestroyQueryPool(vlk_dev, vlk_qpool, NULL);
  vkDestroyCommandPool(vlk_dev, vlk_cpool, NULL);
  vkDestroyDevice(vlk_dev, NULL);
  vkDestroyInstance(vlk_ins, NULL);
}

//}}}

//{{{ [tbf] Tensor Buffers

typedef struct tbf_list {
  vlk_buffer_t data[12];
} tbf_list_t;
static tbf_list_t tbf_create_tensor_param_buffers(unsigned n, unsigned s0, unsigned s1) {
  // These will be loaded from the tensor file, currently requires host
  tbf_list_t res;
  for (int i = 0; i < n; i++) {
    res.data[i] = vlk_create_host_buffer(s0 * (s1 == 0 ? 1 : s1), 0);
  }
  return res;
}

static void tbf_load_tensor(vlk_buffer_t b, const char * name, unsigned s0, unsigned s1, unsigned s2, unsigned s3) {
  void * ptr;
  _(vkMapMemory(vlk_dev, b.mem, 0, VK_WHOLE_SIZE, 0, &ptr));
  sft_get(name, ptr, s0, s1, s2, s3);
  vkUnmapMemory(vlk_dev, b.mem);
}
static void tbf_load_tr_tensor(tbf_list_t l, const char * name, unsigned s0, unsigned s1, unsigned s2, unsigned s3) {
  char buf[256];
  for (int i = 0; i < 12; i++) {
    sprintf(buf, "h.%d.%s", i, name);
    tbf_load_tensor(l.data[i], buf, s0, s1, s2, s3);
  }
}

//}}}

//{{{ uncategorised utils

static VkCommandBuffer alloc() {
  VkCommandBuffer cb = vlk_allocate_command_buffer();
  vlk_begin_command_buffer(cb);
  return cb;
}
static void submit(VkCommandBuffer cb) {
  vlk_end_command_buffer(cb);
  vlk_submit(cb);
}

typedef enum di_e {
  di_1,
  di_64,
  di_768,
  di_1024,
  di_2304,
  di_3072,
  di_max,
} di_et;
static vlk_buffer_t create_indirect_buffer(unsigned tksz) {
  vlk_buffer_t buf = vlk_create_host_buffer(di_max * sizeof(VkDispatchIndirectCommand), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

  VkDispatchIndirectCommand * t;
  _(vkMapMemory(vlk_dev, buf.mem, 0, VK_WHOLE_SIZE, 0, (void **)&t));
  t[di_1   ] = (VkDispatchIndirectCommand) { tksz,    1, 1 };
  t[di_64  ] = (VkDispatchIndirectCommand) { tksz,   64, 1 };
  t[di_768 ] = (VkDispatchIndirectCommand) { tksz,  768, 1 };
  t[di_1024] = (VkDispatchIndirectCommand) { tksz, 1024, 1 };
  t[di_2304] = (VkDispatchIndirectCommand) { tksz, 2304, 1 };
  t[di_3072] = (VkDispatchIndirectCommand) { tksz, 3072, 1 };
  vkUnmapMemory(vlk_dev, buf.mem);

  return buf;
}

static void bind(VkCommandBuffer cb, vlk_ppl_t ppl, ...) {
  va_list args;
  va_start(args, ppl);
  VkDescriptorSet dsets[8];
  for (int i = 0; i < ppl.sets; i++) dsets[i] = va_arg(args, vlk_buffer_t).dset;
  va_end(args);

  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, ppl.ppl);
  vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, ppl.pl, 0, ppl.sets, dsets, 0, NULL);
}

#define push_k(ppl, K) do {                                              \
  unsigned k = (K);                                                      \
  vkCmdPushConstants(cb, ppl.pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &k); \
} while (0);

#define dispatch(ppl, d1, d2, d3, ...) do {                                       \
  bind(cb, ppl, __VA_ARGS__);                                                     \
  vkCmdDispatch(cb, d1, d2, d3);                                                  \
  vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, vlk_qpool, qp++); \
} while (0);
#define dispatch_i(ppl, di, ...) do {                                             \
  bind(cb, ppl, __VA_ARGS__);                                                     \
  vkCmdDispatchIndirect(cb, b_indir.buf, di * sizeof(VkDispatchIndirectCommand)); \
  vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, vlk_qpool, qp++); \
} while (0);

#define B(X) X.data[0]
#define L(X, N) X.data[N]

//}}}

int main() {
  vlk_init();
  byt_init();
  bpe_init();
  enc_init();
  sft_init();

  //{{{ buffers + tensors
  vlk_buffer_t b_input = vlk_create_host_buffer(1024, VK_BUFFER_USAGE_TRANSFER_DST_BIT);

  tbf_list_t b_cattn_b = tbf_create_tensor_param_buffers(12,  2304,    0);
  tbf_list_t b_cattn_w = tbf_create_tensor_param_buffers(12,   768, 2304);
  tbf_list_t b_cproj_b = tbf_create_tensor_param_buffers(12,   768,    0);
  tbf_list_t b_cproj_w = tbf_create_tensor_param_buffers(12,   768,  768);
  tbf_list_t b_ln1b    = tbf_create_tensor_param_buffers(12,   768,    0);
  tbf_list_t b_ln1w    = tbf_create_tensor_param_buffers(12,   768,    0);
  tbf_list_t b_ln2b    = tbf_create_tensor_param_buffers(12,   768,    0);
  tbf_list_t b_ln2w    = tbf_create_tensor_param_buffers(12,   768,    0);
  tbf_list_t b_lnfb    = tbf_create_tensor_param_buffers( 1,   768,    0);
  tbf_list_t b_lnfw    = tbf_create_tensor_param_buffers( 1,   768,    0);
  tbf_list_t b_mlpcf_b = tbf_create_tensor_param_buffers(12,  3072,    0);
  tbf_list_t b_mlpcf_w = tbf_create_tensor_param_buffers(12,   768, 3072);
  tbf_list_t b_mlpcp_b = tbf_create_tensor_param_buffers(12,   768,    0);
  tbf_list_t b_mlpcp_w = tbf_create_tensor_param_buffers(12,   768, 3072);
  tbf_list_t b_wpe     = tbf_create_tensor_param_buffers( 1,  1024,  768);
  tbf_list_t b_wte     = tbf_create_tensor_param_buffers( 1, 50257,  768);

  tbf_load_tensor(b_wte .data[0], "wte.weight",  50257, 768, 0, 0);
  tbf_load_tensor(b_wpe .data[0], "wpe.weight",   1024, 768, 0, 0);
  tbf_load_tensor(b_lnfw.data[0], "ln_f.weight",   768,   0, 0, 0);
  tbf_load_tensor(b_lnfb.data[0], "ln_f.bias",     768,   0, 0, 0);

  tbf_load_tr_tensor(b_ln1w,    "ln_1.weight",         768,    0, 0, 0);
  tbf_load_tr_tensor(b_ln1b,    "ln_1.bias",           768,    0, 0, 0);
  tbf_load_tr_tensor(b_ln2w,    "ln_2.weight",         768,    0, 0, 0);
  tbf_load_tr_tensor(b_ln2b,    "ln_2.bias",           768,    0, 0, 0);
  tbf_load_tr_tensor(b_cattn_w, "attn.c_attn.weight",  768, 2304, 0, 0);
  tbf_load_tr_tensor(b_cattn_b, "attn.c_attn.bias",   2304,    0, 0, 0);
  tbf_load_tr_tensor(b_cproj_w, "attn.c_proj.weight",  768,  768, 0, 0);
  tbf_load_tr_tensor(b_cproj_b, "attn.c_proj.bias",    768,    0, 0, 0);
  tbf_load_tr_tensor(b_mlpcf_w, "mlp.c_fc.weight",     768, 3072, 0, 0);
  tbf_load_tr_tensor(b_mlpcf_b, "mlp.c_fc.bias",      3072,    0, 0, 0);
  tbf_load_tr_tensor(b_mlpcp_w, "mlp.c_proj.weight",  3072,  768, 0, 0);
  tbf_load_tr_tensor(b_mlpcp_b, "mlp.c_proj.bias",     768,    0, 0, 0);

  vlk_buffer_t b_amax0   = vlk_create_local_buffer(  256,        0);
  vlk_buffer_t b_h       = vlk_create_local_buffer( 1024 * 1024, 0);
  vlk_buffer_t b_lmean   = vlk_create_local_buffer( 1024,        0);
  vlk_buffer_t b_logit   = vlk_create_local_buffer(50257,        0);
  vlk_buffer_t b_lvari   = vlk_create_local_buffer( 1024,        0);
  vlk_buffer_t b_mlp     = vlk_create_local_buffer( 1024 * 3072, 0);
  vlk_buffer_t b_qkv     = vlk_create_local_buffer( 1024 * 2304, 0);
  vlk_buffer_t b_x0      = vlk_create_local_buffer( 1024 *  768, 0);
  vlk_buffer_t b_x1      = vlk_create_local_buffer( 1024 *  768, 0);
  vlk_buffer_t b_x2      = vlk_create_local_buffer( 1024 *  768, 0);
  vlk_buffer_t b_xtmp    = vlk_create_local_buffer( 1024 *  768, 0);
  //}}}

  //{{{ pipelines
  vlk_ppl_t p_add2b = vlk_create_pipeline("gpt2-add2b.comp.spv", 2, 0);
  vlk_ppl_t p_amax0 = vlk_create_pipeline("gpt2-amax0.comp.spv", 2, 0);
  vlk_ppl_t p_amax1 = vlk_create_pipeline("gpt2-amax1.comp.spv", 4, 4);
  vlk_ppl_t p_atscr = vlk_create_pipeline("gpt2-atscr.comp.spv", 2, 4);
  vlk_ppl_t p_embed = vlk_create_pipeline("gpt2-embed.comp.spv", 4, 0);
  vlk_ppl_t p_lnear = vlk_create_pipeline("gpt2-lnear.comp.spv", 4, 4);
  vlk_ppl_t p_lnorm = vlk_create_pipeline("gpt2-lnorm.comp.spv", 6, 0);
  vlk_ppl_t p_lnrm2 = vlk_create_pipeline("gpt2-lnrm2.comp.spv", 7, 0);
  vlk_ppl_t p_logit = vlk_create_pipeline("gpt2-logit.comp.spv", 4, 4);
  vlk_ppl_t p_lvari = vlk_create_pipeline("gpt2-lvari.comp.spv", 3, 0);
  vlk_ppl_t p_lvar2 = vlk_create_pipeline("gpt2-lvar2.comp.spv", 4, 0);
  vlk_ppl_t p_pgelu = vlk_create_pipeline("gpt2-pgelu.comp.spv", 1, 0);
  vlk_ppl_t p_plsum = vlk_create_pipeline("gpt2-plsum.comp.spv", 2, 0);
  vlk_ppl_t p_psmax = vlk_create_pipeline("gpt2-psmax.comp.spv", 2, 0);
  vlk_ppl_t p_psum2 = vlk_create_pipeline("gpt2-psum2.comp.spv", 3, 0);
  vlk_ppl_t p_smaxv = vlk_create_pipeline("gpt2-smaxv.comp.spv", 3, 4);
  //}}}

  VkCommandBuffer cb;

  //{{{ load input buffer

  tkn_ids_t ts = tkn_encode(text);

  vlk_buffer_t b_indir = create_indirect_buffer(ts.sz);

  cb = alloc();
  vkCmdUpdateBuffer(cb, b_input.buf, 0, ts.sz * 4, ts.ids);

  bind(cb, p_embed, B(b_wte), B(b_wpe), b_input, b_x0);
  vkCmdDispatchIndirect(cb, b_indir.buf, di_768 * sizeof(VkDispatchIndirectCommand));
  submit(cb);
  //}}}

  //{{{ gpt-2 main loop

  cb = alloc();
  int qp = 0;
  vkCmdResetQueryPool(cb, vlk_qpool, 0, 1024);
  vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, vlk_qpool, qp++);

  //{{{ embedding
  dispatch_i(p_embed, di_768, B(b_wte), B(b_wpe), b_input, b_x0);
  //}}}

  //{{{ transform
  for (int i = 0; i < 12; i++) {
    //{{{ normalisation 1
    dispatch_i(p_plsum, di_1,   b_x0, b_lmean);
    dispatch_i(p_lvari, di_768, b_x0, b_lmean, b_xtmp);
    dispatch_i(p_plsum, di_1,   b_xtmp, b_lvari);
    dispatch_i(p_lnorm, di_768, L(b_ln1w, i), L(b_ln1b, i), b_lmean, b_lvari, b_x0, b_x1);
    //}}}

    //{{{ multi-head attention
    push_k(p_lnear, 768);
    dispatch_i(p_lnear, di_2304, L(b_cattn_w, i), L(b_cattn_b, i), b_x1, b_qkv);

    for (unsigned head = 0; head < 12; head++) {
      // b_qkv contains all data for Q, followed by K, followed by V Then each of
      // QKV is split into heads (12). Or: split 2304 into 3, then each 768 into
      // 12 to be 64 per head
      push_k(p_atscr, head);
      dispatch_i(p_atscr, di_1024, b_qkv, b_h);
      dispatch_i(p_psmax, di_1,    b_h, b_h);
      dispatch_i(p_smaxv, di_64,   b_h, b_qkv, b_x2);
    }

    push_k(p_lnear, 768);
    dispatch_i(p_lnear, di_768, L(b_cproj_w, i), L(b_cproj_b, i), b_x2, b_x1);
    //}}}

    //{{{ residue
    dispatch_i(p_add2b, di_768, b_x1, b_x0);
    //}}}

    //{{{ normalization 2
    dispatch_i(p_plsum, di_1,   b_x0, b_lmean);
    dispatch_i(p_lvari, di_768, b_x0, b_lmean, b_x2);
    dispatch_i(p_plsum, di_1,   b_x2, b_lvari);
    dispatch_i(p_lnorm, di_768, L(b_ln2w, i), L(b_ln2b, i), b_lmean, b_lvari, b_x0, b_x1);
    //}}}

    //{{{ multi-layer perceptron
    dispatch_i(p_lnear, di_3072, L(b_mlpcf_w, i), L(b_mlpcf_b, i), b_x1, b_mlp);
    dispatch_i(p_pgelu, di_3072, b_mlp);
    push_k(p_lnear, 3072);
    dispatch_i(p_lnear, di_768, L(b_mlpcp_w, i), L(b_mlpcp_b, i), b_mlp, b_x1);
    //}}}

    //{{{ residue
    dispatch_i(p_add2b, di_768, b_x1, b_x0);
    //}}}
  }
  //}}}

  //{{{ final normalisation
  dispatch(p_psum2, 1,   1, 1, b_x0, b_lmean, b_indir);
  dispatch(p_lvar2, 1, 768, 1, b_x0, b_lmean, b_x2, b_indir);
  dispatch(p_psum2, 1,   1, 1, b_x2, b_lvari, b_indir);
  dispatch(p_lnrm2, 1, 768, 1, B(b_lnfw), B(b_lnfb), b_lmean, b_lvari, b_x0, b_x1, b_indir);
  //}}}

  //{{{ next token

  //{{{ logit
  dispatch(p_logit, 50257, 1, 1, B(b_wte), b_x1, b_logit, b_indir);
  //}}}

  //{{{ argmax (i.e. next token) directly into input
  dispatch(p_amax0, 256, 1, 1, b_logit, b_amax0);
  dispatch(p_amax1,   1, 1, 1, b_logit, b_amax0, b_input, b_indir);
  //}}}

  vlk_end_command_buffer(cb);
  //}}}

  //}}}

  //{{{ generate N tokens
  int count = 0;
  for (; count < 12; count++) vlk_submit(cb);
  vkDeviceWaitIdle(vlk_dev);
  //}}}

  //{{{ load tokens from GPU and print final text
  ts.sz += count;

  unsigned * t;
  VkDeviceMemory mem = b_input.mem;
  _(vkMapMemory(vlk_dev, mem, 0, VK_WHOLE_SIZE, 0, (void **)&t));
  for (int i = 0; i < ts.sz; i++) ts.ids[i] = t[i];
  vkUnmapMemory(vlk_dev, mem);

  char * buf = calloc(10240, 1);
  tkn_decode(ts, buf, 10240);
  printf("%s\n", buf);
  //}}}

  //{{{ dump timings
  uint64_t data[1024];
  _(vkGetQueryPoolResults(vlk_dev, vlk_qpool, 0, qp, sizeof(data), data, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
  for (int i = 1; i < qp; i++) {
    int64_t d = data[i] - data[i - 1];
    if (d < 100000) continue; // Only the slowest
    printf("%4d -- %12lld\n", i, d);
  }
  printf(" Total: %12lld\n", data[qp - 1] - data[0]);
  //}}}

  vlk_deinit();
}

// vim:fdm=marker
