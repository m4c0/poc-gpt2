# poc-gpt2

Dissecting GPT2

You probably need these files:
* https://openaipublic.blob.core.windows.net/gpt-2/models/124M/vocab.bpe
* https://openaipublic.blob.core.windows.net/gpt-2/models/124M/model.ckpt.data-00000-of-00001

Using `curl`, something like:

```bash
curl -o vocab.bpe https://openaipublic.blob.core.windows.net/gpt-2/models/124M/vocab.bpe
curl -o model.ckpt.data-00000-of-00001 https://openaipublic.blob.core.windows.net/gpt-2/models/124M/model.ckpt.data-00000-of-00001
```
