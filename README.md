# poc-gpt2

Dissecting GPT2. Currently contains the encoder (i.e. the token generation
loop).

## WARNING: Unfiltered Outputs and Toxicity

Please be aware that running this implementation will almost certainly result
in the generation of toxic, politically incorrect, biased, or highly offensive
text - **regardless of your starting prompt.** Even if you input a completely
benign prompt (e.g., "The crocodile is..."), generating a long sequence of
tokens will inevitably cause the output to devolve into internet rants,
conspiracy theories, or offensive material. This is not a bug in the code, but
rather a perfect storm of how early base models interact with specific decoding
math.

Why this happens?

This implementation uses deterministic, greedy decoding (no temperature) paired
with a strict repetition penalty. To avoid repeating standard words, the model
is mathematically forced to continuously pick its 2nd, 3rd, or 10th choice
tokens. Over a long sequence (e.g., 512 tokens), this pushes the model away
from normal sentence structure and deep into the obscure, highly opinionated
extremes of its vocabulary.

GPT-2 was trained on WebText, an unfiltered dataset scraped from heavily
upvoted Reddit links in the mid-2010s. Statistically, the text in its latent
space that constantly shifts topics and avoids standard repetition closely
mirrors unhinged internet forum discourse.

The HuggingFace/OpenAI weights used here belong to a pure "base model".
Unlike modern conversational AI, this model has not undergone human feedback.
It has no safety filters, no guardrails, and no concept of appropriateness. 

**Disclaimer:** This repository is intended strictly for educational purposes
to demonstrate the raw, underlying mechanics of early Large Language Models.
**Do not use this implementation in user-facing applications or production
environments.**

## The implementation

Code:
* `build.c`      - builder
* `gpt2.c`       - self-contained implementation
* `gpt2-naive.c` - self-contained implementation, without optimisations (should
                   be easier to read)
* `*.comp`       - compute shaders written in GLSL

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
