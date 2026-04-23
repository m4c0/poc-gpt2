#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_WARNINGS
#include <process.h>
#else
#include <unistd.h>
#endif

#include <sys/stat.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define EXE_EXT ".exe"
#else
#define EXE_EXT ""
#endif
#define EXE(X) X EXE_EXT

#ifdef __clang__
#  define CC "clang"
#elif _MSC_VER
#  define CC "cl"
#else
#  define CC "cc"
#endif

static void usage() {
  fprintf(stderr, "just call 'build' without arguments\n");
}

static uint64_t mtime(const char * name) {
#ifdef _WIN32
  struct __stat64 s = {0};
  _stat64(name, &s);
  return s.st_mtime;
#else
  struct stat t;
  if (0 != stat(name, &t)) return 0;
  return t.st_mtimespec.tv_sec * 1000ULL + t.st_mtimespec.tv_nsec / 1000000;
#endif
}

static int run(char ** args) {
  assert(args && args[0]);

#ifdef _WIN32
  if (0 == _spawnvp(_P_WAIT, args[0], (const char * const *)args)) {
    return 0;
  }
#else
  pid_t pid = fork();
  if (pid == 0) {
    execvp(args[0], args);
    abort();
  } else if (pid > 0) {
    int sl = 0;
    assert(0 <= waitpid(pid, &sl, 0));
    if (WIFEXITED(sl)) return WEXITSTATUS(sl);
  }
#endif

  fprintf(stderr, "failed to run child process: %s\n", args[0]);
  return 1;
}

static int shader(char * name) {
  char spv[1024];
  sprintf(spv, "%s.spv", name);

  if (mtime(name) < mtime(spv)) return 0;

  char * args[] = { EXE("glslang"), "-V", name, "-o", spv, 0 };
  return run(args);
}

static int compile(char * name) {
  char src[1024];
  sprintf(src, "%s.c", name);

  char exe[1024];
  sprintf(exe, "%s" EXE_EXT, name);

  if (mtime(src) < mtime(exe)) return 0;

  char * args[] = { EXE(CC), "-Wall", "-g", "-IVulkan-Headers/include", "-o", exe, src, 0 };
  if (run(args)) return 1;

  fprintf(stderr, "%s\n", src);
  return 0;
}

int main(int argc, char ** argv) {
  if (argc != 1) return (usage(), 1);

  if (shader("vulkan.comp"    )) return 1;
  if (shader("gpt2-add2b.comp")) return 1;
  if (shader("gpt2-addb2.comp")) return 1;
  if (shader("gpt2-amax0.comp")) return 1;
  if (shader("gpt2-amax1.comp")) return 1;
  if (shader("gpt2-atscr.comp")) return 1;
  if (shader("gpt2-embed.comp")) return 1;
  if (shader("gpt2-line2.comp")) return 1;
  if (shader("gpt2-lnear.comp")) return 1;
  if (shader("gpt2-lnorm.comp")) return 1;
  if (shader("gpt2-lnrm2.comp")) return 1;
  if (shader("gpt2-logit.comp")) return 1;
  if (shader("gpt2-lvari.comp")) return 1;
  if (shader("gpt2-lvar2.comp")) return 1;
  if (shader("gpt2-pgelu.comp")) return 1;
  if (shader("gpt2-plsum.comp")) return 1;
  if (shader("gpt2-psmax.comp")) return 1;
  if (shader("gpt2-psum2.comp")) return 1;
  if (shader("gpt2-smaxv.comp")) return 1;
  if (shader("gpt2-smax2.comp")) return 1;
  if (shader("gpt2-smxv2.comp")) return 1;

  if (compile("vocab"     )) return 1;
  if (compile("safetensor")) return 1;
  if (compile("vulkan"    )) return 1;
  if (compile("gpt2"      )) return 1;
  if (compile("gpt2-naive")) return 1;

  return 0;
}
