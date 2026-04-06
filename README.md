# poc-gpt2

Dissecting GPT2

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
