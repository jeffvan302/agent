However, Ollama does support some concurrency:
- Multiple different models can run in parallel (e.g., one queue for medgemma, another for embeddings)
- A single model instance can interleave requests if they're light enough

For your use case, if you want true parallel image processing, you'd need:
1. A queue/worker system that sends images to Ollama as workers become available
2. Or run two Ollama instances on different ports