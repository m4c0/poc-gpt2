#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_WARNINGS
#include <process.h>
#else
#include <unistd.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define EXE(X) X ".exe"
#else
#define EXE(X) X
#endif

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

int main(int argc, char ** argv) {
  if (argc != 1) return (usage(), 1);

  { char * args[] = { EXE(CC), "-Wall", "-g", "-o", EXE("vocab"), "vocab.c", 0 };
    if (run(args)) return 1; }
  { char * args[] = { EXE(CC), "-Wall", "-g", "-o", EXE("safetensor"), "safetensor.c", 0 };
    if (run(args)) return 1; }
  { char * args[] = { EXE("glslang"), "-V", "vulkan.comp", "-o", "vulkan.comp.spv", 0 };
    if (run(args)) return 1; }
  { char * args[] = { EXE(CC), "-Wall", "-g", "-IVulkan-Headers/include", "-o", EXE("vulkan"), "vulkan.c", 0 };
    if (run(args)) return 1; }
  { char * args[] = { EXE("glslang"), "-V", "gpt2-cattn.comp", "-o", "gpt2-cattn.comp.spv", 0 };
    if (run(args)) return 1; }
  { char * args[] = { EXE(CC), "-Wall", "-g", "-IVulkan-Headers/include", "-o", EXE("gpt2"), "gpt2.c", 0 };
    if (run(args)) return 1; }

  return 0;
}
