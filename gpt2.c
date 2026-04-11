#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define VOLK_IMPLEMENTATION
#include "volk.h"

#include "Vulkan-Headers/include/vulkan/vulkan_core.h"

#define _(X) assert(VK_SUCCESS == (X));

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
static char byt_rev_map[65536] = {0};

static wchar_t * byt_encode_bytes(const char * b, unsigned bytes) {
  wchar_t * mb = malloc(sizeof(wchar_t) * bytes);
  for (int i = 0; i < bytes; i++) mb[i] = byt_map[(unsigned)b[i]];
  return mb;
}
// static int byt_decode_bytes(utl_wstr_t str, char * dst, unsigned dsz) {
//   int i;
//   for (i = 0; i < str.sz && i < dsz; i++) dst[i] = byt_rev_map[str.str[i]];
//   return i;
// }
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
//static int tkn_decode(tkn_ids_t ts, char * buf, int bsz) {
//  int total = 0;
//  for (int i = 0; i < ts.sz && i < bsz; i++) {
//    utl_wstr_t tk = enc_map[ts.ids[i]];
//    total += byt_decode_bytes(tk, buf + total, bsz - total);
//  }
//  if (total < bsz) buf[total] = 0;
//  return total;
//}

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

static VkCommandBuffer vlk_cb;
static VkCommandPool vlk_cpool;
static VkDescriptorPool vlk_dpool;
static VkDescriptorSetLayout vlk_dsl;
static VkPhysicalDevice vlk_pd;
static VkPipeline vlk_ppls[1];
static VkPipelineLayout vlk_pls[1];
static VkQueue vlk_q;
static unsigned vlk_qf;

static inline VkDevice vlk_dev() { return volkGetLoadedDevice(); }

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
  VkInstance res;
  _(vkCreateInstance(&info, NULL, &res));
  volkLoadInstance(res);
}

static void vlk_find_physical_device() {
  VkPhysicalDevice pd[16];
  uint32_t pdsz = 16;
  _(vkEnumeratePhysicalDevices(volkGetLoadedInstance(), &pdsz, pd));
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
  };
#ifdef __APPLE__
  const char * ext[1] = { "VK_KHR_portability_subset" };
  info.ppEnabledExtensionNames = ext;
  info.enabledExtensionCount = 1;
#endif
  VkDevice res;
  _(vkCreateDevice(vlk_pd, &info, NULL, &res));
  volkLoadDevice(res);

  vkGetDeviceQueue(res, vlk_qf, 0, &vlk_q);
}

static VkShaderModule vlk_create_shader_module() {
  FILE * f = fopen("vulkan.comp.spv", "rb");
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
  _(vkCreateShaderModule(vlk_dev(), &info, NULL, &mod));

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
  _(vkCreateDescriptorSetLayout(vlk_dev(), &info, NULL, &vlk_dsl));
}

static void vlk_create_descriptor_pool() {
  VkDescriptorPoolSize pszs[1] = {{
    .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 32,
  }};
  VkDescriptorPoolCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .maxSets = 32,
    .poolSizeCount = 1,
    .pPoolSizes = pszs,
  };
  _(vkCreateDescriptorPool(vlk_dev(), &info, NULL, &vlk_dpool));
}

static void vlk_create_pipeline_layouts() {
  VkPipelineLayoutCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 1,
    .pSetLayouts = &vlk_dsl,
  };
  _(vkCreatePipelineLayout(vlk_dev(), &info, NULL, vlk_pls));
}
static void vlk_create_pipelines() {
  VkShaderModule mod = vlk_create_shader_module();

  VkComputePipelineCreateInfo infos[] = {{
    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .pName = "main",
      .module = mod,
    },
    .layout = vlk_pls[0],
  }};

  _(vkCreateComputePipelines(vlk_dev(), NULL, 1, infos, NULL, vlk_ppls));
  vkDestroyShaderModule(vlk_dev(), mod, NULL);
}

typedef struct vlk_buffer {
  VkBuffer buf;
  VkDeviceMemory mem;
  VkDescriptorSet dset;
} vlk_buffer_t;
static vlk_buffer_t vlk_create_buffer(VkDeviceSize sz, VkMemoryPropertyFlags mem_flags, VkBufferUsageFlags ex_flags) {
  VkPhysicalDeviceMemoryProperties props;
  vkGetPhysicalDeviceMemoryProperties(vlk_pd, &props);

  for (int i = 0; i < props.memoryTypeCount; i++) {
    VkMemoryPropertyFlags flags = props.memoryTypes[i].propertyFlags;
    if ((flags & mem_flags) != mem_flags) continue;

    vlk_buffer_t res;

    VkBufferCreateInfo buf = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = sz * sizeof(float),
      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | ex_flags,
    };
    _(vkCreateBuffer(vlk_dev(), &buf, NULL, &res.buf));

    VkMemoryAllocateInfo mem = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = sz * sizeof(float),
      .memoryTypeIndex = i,
    };
    _(vkAllocateMemory(vlk_dev(), &mem, NULL, &res.mem));
    _(vkBindBufferMemory(vlk_dev(), res.buf, res.mem, 0));

    VkDescriptorSetAllocateInfo ds = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = vlk_dpool,
      .descriptorSetCount = 1,
      .pSetLayouts = &vlk_dsl,
    };
    _(vkAllocateDescriptorSets(vlk_dev(), &ds, &res.dset));

    VkDescriptorBufferInfo dbi = { res.buf, 0, VK_WHOLE_SIZE };
    VkWriteDescriptorSet wr[] = {{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = res.dset,
      .dstBinding = 0,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &dbi,
    }};
    vkUpdateDescriptorSets(vlk_dev(), 1, wr, 0, NULL);

    return res;
  }
  unreachable("could not find host memory with Vulkan");
}
static vlk_buffer_t vlk_create_host_buffer(VkDeviceSize sz, VkBufferUsageFlags ex_flags) {
  return vlk_create_buffer(sz,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      ex_flags);
}
// static vlk_buffer_t vlk_create_local_buffer(VkDeviceSize sz, VkBufferUsageFlags ex_flags) {
//   return vlk_create_buffer(sz, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ex_flags);
// }
static void vlk_destroy_buffer(vlk_buffer_t b) {
  vkDestroyBuffer(vlk_dev(), b.buf, NULL);
  vkFreeMemory(vlk_dev(), b.mem, NULL);
}

static void vlk_create_command_pool() {
  VkCommandPoolCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
  };
  _(vkCreateCommandPool(vlk_dev(), &info, NULL, &vlk_cpool));
}

static void vlk_create_command_buffer() {
  VkCommandBufferAllocateInfo info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = vlk_cpool,
    .commandBufferCount = 1,
  };
  _(vkAllocateCommandBuffers(vlk_dev(), &info, &vlk_cb));
}

static void vlk_begin_command_buffer() {
  VkCommandBufferBeginInfo info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
  };
  vkBeginCommandBuffer(vlk_cb, &info);
}
static void vlk_end_command_buffer() {
  vkEndCommandBuffer(vlk_cb);
}

static void vlk_submit() {
  VkSubmitInfo info = {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pCommandBuffers = &vlk_cb,
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
  vlk_create_pipeline_layouts();
  vlk_create_pipelines();
  vlk_create_command_pool();
  vlk_create_command_buffer();
}
static void vlk_deinit() {
  for (int i = 0; i < 1; i++) vkDestroyPipelineLayout(vlk_dev(), vlk_pls[i], NULL);
  for (int i = 0; i < 1; i++) vkDestroyPipeline(vlk_dev(), vlk_ppls[i], NULL);
  vkDestroyDescriptorSetLayout(vlk_dev(), vlk_dsl, NULL);
  vkDestroyDescriptorPool(vlk_dev(), vlk_dpool, NULL);
  vkDestroyCommandPool(vlk_dev(), vlk_cpool, NULL);
  vkDestroyDevice(vlk_dev(), NULL);
  vkDestroyInstance(volkGetLoadedInstance(), NULL);
}

//}}}

static void load_tensor(vlk_buffer_t b, const char * name, unsigned s0, unsigned s1, unsigned s2, unsigned s3) {
  void * ptr;
  _(vkMapMemory(vlk_dev(), b.mem, 0, VK_WHOLE_SIZE, 0, &ptr));
  sft_get(name, ptr, s0, s1, s2, s3);
  vkUnmapMemory(vlk_dev(), b.mem);
}

int main() {
  byt_init();
  bpe_init();
  enc_init();
  sft_init();
  vlk_init();

  vlk_buffer_t b_y = vlk_create_host_buffer(768, 0);
  vlk_buffer_t b_cattn_w = vlk_create_host_buffer(768 * 2304, 0);
  vlk_buffer_t b_cattn_b = vlk_create_host_buffer(2304, 0);

  const char * text = "The quick brown fox jumps over the lazy dog.";
  tkn_ids_t ts = tkn_encode(text);

  // Embedding

  float wpe[768]; sft_get_row("wpe.weight", 0, wpe, 768);
  float wte[768]; sft_get_row("wte.weight", ts.ids[0], wte, 768);
  float x[768]; for (int i = 0; i < 768; i++) x[i] = wpe[i] + wte[i];

  // Normalisation of Layer 1

  float ln1w[768]; sft_get("h.0.ln_1.weight", ln1w, 768, 0, 0, 0);
  float ln1b[768]; sft_get("h.0.ln_1.bias", ln1b, 768, 0, 0, 0);

  float mean = 0;
  for (int i = 0; i < 768; i++) mean += x[i];
  mean /= 768;

  float var = 0;
  for (int i = 0; i < 768; i++) var += (x[i] - mean) * (x[i] - mean);
  var /= 768;

  float * y;
  _(vkMapMemory(vlk_dev(), b_y.mem, 0, VK_WHOLE_SIZE, 0, (void **)&y));
  for (int i = 0; i < 768; i++) y[i] = ln1b[i] + ln1w[i] * (x[i] - mean) / sqrtf(var + 1e-5);
  vkUnmapMemory(vlk_dev(), b_y.mem);

  // Attention Layer 1

  // attn.c_attn contains all data for Q, followed by K, followed by V
  // Then each of QKV is split into heads (12)
  load_tensor(b_cattn_w, "h.0.attn.c_attn.weight", 768, 2304, 0, 0);
  load_tensor(b_cattn_b, "h.0.attn.c_attn.bias", 2304, 0, 0, 0);

  vlk_begin_command_buffer();

  // vkCmdUpdateBuffer(vlk_cb, vlk_bufs[2], 0, 768 * sizeof(float), y);
  //vkCmdFillBuffer(vlk_cb, vlk_bufs[);
  vkCmdBindPipeline(vlk_cb, VK_PIPELINE_BIND_POINT_COMPUTE, vlk_ppls[0]);
  // vkCmdBindDescriptorSets(vlk_cb, VK_PIPELINE_BIND_POINT_COMPUTE, vlk_pls[0], 0, 1, vlk_dset, 0, NULL);
  vkCmdDispatch(vlk_cb, 1, 1, 1);

  vlk_end_command_buffer();
  vlk_submit();
  vkDeviceWaitIdle(vlk_dev());

  vlk_destroy_buffer(b_y);
  vlk_destroy_buffer(b_cattn_w);
  vlk_destroy_buffer(b_cattn_b);
  vlk_deinit();
}
