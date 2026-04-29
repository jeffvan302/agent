# Ollama CPU & GPU Tuning Guide

This guide explains how to control CPU threads, GPU usage, and hybrid execution when running models with Ollama from the command line.

---

## 🧠 Basic Command

```bash
ollama run gemma4:e2b
```

---

## ⚙️ Control CPU Threads

Use the `OLLAMA_NUM_THREADS` environment variable (or configure per-model `ollama_num_threads` in the app):

```bash
OLLAMA_NUM_THREADS=8 ollama run gemma4:e2b
```

### In-App Model Editor
When editing an Ollama model, set **CPU threads** to a positive value to override the default. 0 lets Ollama decide automatically.

### Notes
- Defaults to number of **physical CPU cores**
- Using too many threads can reduce performance
- Recommended: match physical cores (not hyperthreads)

---

## 🖥️ Force CPU-Only Mode

Disable GPU usage:

```bash
OLLAMA_NO_GPU=1 ollama run gemma4:e2b
```

Alternative method:

```bash
CUDA_VISIBLE_DEVICES="" ollama run gemma4:e2b
```

---

## 🚀 GPU Usage (Default)

If your system supports CUDA, Metal, or ROCm, Ollama will automatically use the GPU.

No extra configuration is required.

---

## 🎯 Control GPU Offloading

Use `OLLAMA_GPU_LAYERS` to control how many model layers run on GPU:

```bash
OLLAMA_GPU_LAYERS=20 ollama run gemma4:e2b
```

### Explanation
- Higher value = more GPU usage
- Requires more VRAM
- Improves performance if GPU is available

### Full GPU Example

```bash
OLLAMA_GPU_LAYERS=999 ollama run gemma4:e2b
```

---

## ⚖️ Hybrid CPU + GPU Mode

Ollama automatically splits work between CPU and GPU when needed.

Example configuration:

```bash
OLLAMA_NUM_THREADS=8 \
OLLAMA_GPU_LAYERS=30 \
ollama run gemma4:e2b
```

### Behavior
- First 30 layers → GPU
- Remaining layers → CPU
- CPU threads handle the CPU portion

---

## 🧪 Advanced Tuning

### Batch Size

```bash
OLLAMA_BATCH_SIZE=512 ollama run gemma4:e2b
```

### Context Length

```bash
OLLAMA_CONTEXT_LENGTH=8192 ollama run gemma4:e2b
```

---

## 🧩 Advanced Alternative: llama.cpp

If you need finer control, consider using llama.cpp directly.

Example:

```bash
./main -m model.gguf -t 8 --n-gpu-layers 30
```

---

## ⚠️ Notes

- Ollama prioritizes ease of use over fine-grained control
- Some environment variables are undocumented and may change
- For maximum tuning flexibility, use lower-level tools like llama.cpp

---

## ✅ Recommended Configurations

### CPU-Only System

```bash
OLLAMA_NO_GPU=1 OLLAMA_NUM_THREADS=8 ollama run gemma4:e2b
```

### Mid-Range GPU (8–12GB VRAM)

```bash
OLLAMA_GPU_LAYERS=20 ollama run gemma4:e2b
```

### High-End GPU (24GB+ VRAM)

```bash
OLLAMA_GPU_LAYERS=999 ollama run gemma4:e2b
```

---

## 📌 Summary

| Setting | Purpose |
|--------|--------|
| `OLLAMA_NUM_THREADS` | Control CPU threads |
| `OLLAMA_NO_GPU` | Disable GPU |
| `OLLAMA_GPU_LAYERS` | Control GPU offloading |
| `OLLAMA_BATCH_SIZE` | Throughput tuning |
| `OLLAMA_CONTEXT_LENGTH` | Context window size |

---

This setup allows flexible CPU, GPU, and hybrid execution depending on your hardware.

