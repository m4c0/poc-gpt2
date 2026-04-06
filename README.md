# poc-gpt2

Dissecting GPT2

Code so far:
* `vocab.c` - tokenisation
* `safetensor.c` - reading tensors file

No code dependencies, it should be buildable with any C compiler.

The code is not for production. It is meant for short-lived processes, so just
leaks memory and resources. Also, it will not work fine if you define NDEBUG
because `assert`s are used for runtime logic.

You probably need these files:
* https://openaipublic.blob.core.windows.net/gpt-2/models/124M/vocab.bpe
* https://openaipublic.blob.core.windows.net/gpt-2/models/124M/encoder.json
* https://huggingface.co/openai-community/gpt2/blob/main/model.safetensors

Using `curl`, something like:

```bash
curl -o vocab.bpe https://openaipublic.blob.core.windows.net/gpt-2/models/124M/vocab.bpe
curl -o encoder.json https://openaipublic.blob.core.windows.net/gpt-2/models/124M/encoder.json
curl -o model.safetensors https://huggingface.co/openai-community/gpt2/resolve/main/model.safetensors?download=true
```
