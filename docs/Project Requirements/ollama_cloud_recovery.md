# Ollama Cloud Recovery Specification

## Objective
Recover and re-integrate Ollama Cloud provider support into the existing Provider Request Queue system. This document captures the original Ollama Cloud implementation from the stash, and specifies how to re-apply it correctly over the restored working tree.

## Part 1: Files To Restore From Stash

### 1. `src/ollama_api_client.cpp` (Full Implementation — NEW FILE)
The stash added a new file `src/ollama_api_client.cpp` (was absent from the old commit `e87d108`). It contains the entire Ollama Cloud implementation alongside Ollama Local functions.

**Key components:**
- `IsOllamaCloudProvider()` — checks via `NormalizeProviderType(provider.provider_type) == "ollama_cloud"` AND `LowerAscii(Trim(provider.base_url)) == LowerAscii(Trim(std::string(kOllamaCloudDefaultUrl)))`
- `IsOllamaCloudModelId()` — matches `:cloud` suffix in model ID
- `PostOllamaApiChat()` — sends to `/api/chat` with optional Bearer `api_key`
- `RunOllamaCloudHttpChat()` — streaming chat via Ollama /api/chat
- `RunOllamaCloudHttpCompletion()` — non-streaming simple completion via /api/chat
- `RunOllamaCloudHttpToolPrompt()` — tool-aware streaming via /api/chat
- `TestOllamaCloudEmbeddingConnection()` — probes `/api/embed` with Bearer auth
- `IsOllamaCloudModelAvailable()` — calls `/api/tags` with Bearer auth
- `BuildOllamaApiBody()` — constructs Ollama-native JSON request body
- `ExtractThinkingAndContent()` — parses `<think>...</think>` tags in response
- `ConsumeOllamaApiStreamLine()` — parses NDJSON stream lines from Ollama

**HTTP Architecture:**
- Uses WinHTTP directly with `UniqueHandle` RAII wrappers
- Default base URL: `"https://ollama.com/api"` (from `provider_profiles.h`)
- Sends `Authorization: Bearer <api_key>` when `api_key` is non-empty

**Request Body Format:**
```json
{
  "model": "model_id",
  "stream": true/false,
  "messages": [...],
  "options": {
    "temperature": 0.2,
    "num_predict": 1024,
    "num_thread": 4,
    "num_gpu": 0,
    "num_ctx": 8192
  },
  "keep_alive": 300,
  "tools": [...],
  "think": "high"
}
```

### 2. `src/ollama_api_client.h` (Extended)
The stash extended the header with these new declarations (lines 50–67):
```cpp
bool IsOllamaCloudProvider(const ProviderConfig& provider);
bool IsOllamaCloudModelAvailable(const ProviderConfig& provider, const ModelConfig& model, std::string* error);
bool TestOllamaCloudEmbeddingConnection(const ProviderConfig& provider, const ModelConfig& model, std::string* message);
ChatCompletionResult RunOllamaCloudHttpCompletion(...);
ChatExecutionResult RunOllamaCloudHttpChat(...);
ChatCompletionResult RunOllamaCloudHttpToolPrompt(...);
```

Note: `PostOllamaApiChat()` was originally declared but may have been implemented inline.

### 3. `src/openai_client.h` (Minimal Changes)
The stash made **minimal changes** to `openai_client.h`:
- No `ProviderRequestGate`, `GateSlot`, `GateDomain`, or queue structures were present in the stash version.
- The stash version **only** added ` binding_model_id`, `binding_depth`, and `binding_cooldown_until` fields to `ChatRequestOptions`. These may already exist in the queue system version.
- No changes to `OpenAIClient` method signatures beyond what was already there.

### 4. `src/openai_client.cpp` (Routing Changes + Queue Placeholders)
The stash made these specific routing changes in `openai_client.cpp`:
- `TestConnection()` — added `IsOllamaCloudProvider()` branch that calls `TestOllamaCloudEmbeddingConnection()` for embeddings, or `IsOllamaCloudModelAvailable()` + ping via `RunOllamaCloudHttpChat()` for chat models.
- `StreamChat()` — added `IsOllamaCloudProvider()` branch routing to `RunOllamaCloudHttpChat()`.
- `CreateToolAwareCompletion()` — added `IsOllamaCloudProvider()` branch routing to `RunOllamaCloudHttpToolPrompt()`.
- `CreateSimpleCompletion()` — added `IsOllamaCloudProvider()` branch routing to `RunOllamaCloudHttpCompletion()`.

**Important:** The stash did **NOT** contain the `ProviderRequestGate` implementation. The queue system is new post-stash or was added after the stash was created.

### 5. `src/provider_manager.cpp` (UI Changes)
The stash added the `"Ollama Cloud"` option to the provider type combo box:
- Enum index: `OllamaCloud = 5` (between `BindingProvider` at 4 and the old `BindingProvider` at 5)
- Combo box string at index 5: `"Ollama Cloud"`
- `UpdateForType()` — shows/hides API key field as visible for Ollama Cloud
- `ValidateAndSave()` — captures `api_key` from the edit control when type is `OllamaCloud`
- `DefaultBaseUrl` for Ollama Cloud: `"https://ollama.com/api"`

### 6. `src/types.h` (Queue Fields Added)
The stash added these fields to `ModelConfig`:
```cpp
int max_active_requests = 0;       // 0 = use provider default
int max_queue_size = 0;            // 0 = use provider default
bool self_managed_queue = false;   // bypass local gate
```
And added to `ProviderConfig`:
```cpp
int max_active_requests = 0;
int max_queue_size = 0;
```

### 7. `src/storage.cpp` (Persistence)
The stash added persistence for the three new `ModelConfig` fields in `ModelToJson()` and `ModelFromJson()`:
- `max_active_requests`
- `max_queue_size`
- `self_managed_queue`

### 8. `src/provider_profiles.h` (Normalization)
Added `ollama_cloud` normalization mapping.

## Part 2: The Existing Queue System (To Be Preserved)

The queue system exists **after** the stash was created. It must be preserved when restoring the stash. The queue system consists of:

1. **`ProviderRequestGate`** class in `openai_client.h`:
   - `Configure()` — populates `s_gate_provider_cache` from provider list
   - `Acquire()` — blocks/waits until capacity available
   - `Release()` — frees capacity, notifies next waiter

2. **`GateSlot`** RAII wrapper:
   - Acquires gate on construction
   - Releases gate on destruction

3. **`GateDomain` enum**:
   - `Chat`, `Embedding`

4. **Gate enforcement in `openai_client.cpp`**:
   - `StreamChat()` — wraps with `GateSlot(request, GateDomain::Chat)`
   - `CreateToolAwareCompletion()` — wraps with `GateSlot`
   - `CreateSimpleCompletion()` — wraps with `GateSlot`
   - `StreamToolAwareCompletion()` — wraps with `GateSlot` (added in this session, may not be in stash)
   - `CreateEmbedding()` — declared but not fully implemented yet

## Part 3: Integration Specification

When restoring the stash and rebuilding Ollama Cloud support, the Ollama Cloud provider **must participate in the queue system** exactly like all other providers:

### Routing Rules (OpenAIClient entry points):
All entry points must check `IsOllamaCloudProvider()` **after** `GateSlot::Acquire()` succeeds.

`StreamChat()`:
1. `GateSlot slot(request, GateDomain::Chat)`
2. `if (!slot.Acquire(...)) return queue_full_error`
3. `if (IsOllamaLocalProvider) → RunOllamaLocalHttpChat()`
4. `if (IsOllamaCloudProvider) → RunOllamaCloudHttpChat()`
5. `else → RunRequest()`

`CreateToolAwareCompletion()`:
1. `GateSlot slot(request, GateDomain::Chat)`
2. `if (!slot.Acquire(...)) return queue_full_error`
3. `if (IsOllamaLocalProvider) → RunOllamaLocalHttpToolPrompt()`
4. `if (IsOllamaCloudProvider) → RunOllamaCloudHttpToolPrompt()`
5. `else → HTTP OpenAI-compatible`

`CreateSimpleCompletion()`:
1. `GateSlot slot(request, GateDomain::Chat)`
2. `if (!slot.Acquire(...)) return queue_full_error`
3. `if (IsOllamaLocalProvider) → RunOllamaLocalHttpCompletion()`
4. `if (IsOllamaCloudProvider) → RunOllamaCloudHttpCompletion()`
5. `else → HTTP OpenAI-compatible`

`StreamToolAwareCompletion()`:
1. `GateSlot slot(request, GateDomain::Chat)`
2. `if (!slot.Acquire(...)) return queue_full_error`
3. `if (IsOllamaLocalProvider) → RunOllamaLocalHttpToolPrompt()`
4. `if (IsOllamaCloudProvider) → RunOllamaCloudHttpToolPrompt()`
5. `else → HTTP OpenAI-compatible`

`CreateEmbedding()`:
1. `GateSlot slot(gate_request, GateDomain::Embedding)`
2. `if (!slot.Acquire(...)) throw queue_full_error`
3. `if (IsOllamaLocalProvider) → Ollama /api/embed`
4. `if (IsOllamaCloudProvider) → Ollama Cloud /api/embed with Bearer auth`
5. `else → OpenAI-compatible /embeddings`

### API Key Handling:
Ollama Cloud uses Bearer auth:
- The `ProviderConfig.api_key` field is populated from the provider editor UI.
- In `PostOllamaApiChat()`, `RunOllamaCloudHttpChat()`, and all cloud HTTP functions, pass `provider.api_key`.
- When `api_key` is non-empty, add header: `Authorization: Bearer <api_key>`.

### Model Availability Check:
- `IsOllamaCloudModelAvailable()` queries `GET /api/tags` with Bearer auth.
- Parses response JSON for `"models"` array and checks for `model.id` presence.

### Connection Test:
- For embedding models: call `TestOllamaCloudEmbeddingConnection()` which POSTs to `/api/embed` with `"model"` and `"input": ["test"]`.
- For chat models: call `IsOllamaCloudModelAvailable()` first, then send a ping via `RunOllamaCloudHttpChat()`.

## Part 4: Recovery Steps (Execution Plan)

1. **Document captured** ✓ (this file)
2. **Stash current work:** `git stash push -m "Ollama Cloud queue system"`
3. **Restore original stash:** `git stash pop stash@{1}` (the original stash)
4. **Verify queue system exists** in `openai_client.h` and `openai_client.cpp`
5. **If queue system is missing** (because it was created after the original stash), **reapply it first.**
6. **Apply Ollama Cloud changes** using this document as the guide:
   - Ensure `ollama_api_client.cpp` exists (restore from this session's stash if needed)
   - Wire Ollama Cloud routing **inside** the gate wrappers in all `OpenAIClient` entry points
   - Ensure all cloud functions pass `api_key` for Bearer auth
   - Ensure `ProviderRequestGate::Configure()` is called from `SetProviderCache()`
7. **Build and test**

---

**This document was generated from analysis of:**
- `git diff e87d108 stash@{0} -- src/ollama_api_client.cpp src/openai_client.cpp src/openai_client.h src/provider_manager.cpp src/types.h src/storage.cpp`
- Direct file reads of the stash version via `git show stash@{0}:<file>`
- The current working tree (post-session changes)

**Author:** Auto-generated recovery analysis
**Date:** 2026-05-03
