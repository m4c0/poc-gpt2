# poc-gpt2

Dissecting GPT2. Currently contains the encoder (i.e. the token generation
loop).

Code:
* `build.c` - builder
* `gpt2.c` - self-contained implementation
* `*.comp` - compute shaders written in GLSL

Code of how certain bits work:
* `vocab.c`      - tokenisation
* `safetensor.c` - reading tensors file
* `vulkan.c`     - Vulkan compute shaders

No library usages, it should be buildable as long as you have C compiler.

The code is not for production. It is meant for short-lived processes, so just
leaks memory and resources. Also, it will not work fine if you define NDEBUG
because `assert`s are used for runtime logic.

You need these files:
* https://openaipublic.blob.core.windows.net/gpt-2/models/124M/vocab.bpe
* https://openaipublic.blob.core.windows.net/gpt-2/models/124M/encoder.json
* https://huggingface.co/openai-community/gpt2/blob/main/model.safetensors

Using `curl`, something like:

```bash
curl -o vocab.bpe https://openaipublic.blob.core.windows.net/gpt-2/models/124M/vocab.bpe
curl -o encoder.json https://openaipublic.blob.core.windows.net/gpt-2/models/124M/encoder.json
curl -L -o model.safetensors https://huggingface.co/openai-community/gpt2/resolve/main/model.safetensors?download=true
```

Compiling each C file is a simple `cc` invocation. But, if you have access to
`glslang` you can use `build.c` to automate the build:

```bash
clang -o build build.c
./build
./gpt2
```

The output should be exactly like this:

```
What's the capital of France?

The capital of France is Paris. It's the
```

Followed by some timings. Note: On OSX, the vast majority of the time is spent
on the first step (no matter what's the first step) - looks like MoltenVK does
some shenanigans right before the first pipeline runs.

If you increase the number of generated tokens, you will notice two things:
1. It gets progressive slower - that's why we need the so-called "KV-cache"
2. It repeats itself about "the capital" - that's why people usually add a
   "token penalty" to mitigate that.

