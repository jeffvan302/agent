# Centralized Provider Request Queue Design Document

## 1. Current State Analysis

### 1.1 What Exists Today
- `ProviderConfig` stores `max_active_requests` and `max_queue_size` (provider-level only).
- `ModelConfig` stores `ollama_keep_alive_seconds`, `ollama_num_threads`, `ollama_gpu_layers`, and `ollama_context_length`.
- `ProviderQueueStatus` struct exists for UI telemetry (position, depth, active, max).
- `OpenAIClient` accepts `on_queue_status` callbacks on every public method, but **the parameter is completely unused**.
- `ImageVisionWorkerQueue` exists in `rag_service.cpp` but is **Ollama-only**.
- `DescribeImageWithProviderQueue` (Pick Provider image ingest) calls `OpenAIClient::CreateSimpleCompletion` **directly** with no gate.

### 1.2 What Is Broken
- Image Ingest "Pick Provider" bypasses all queueing.
- All chat, completion, embedding, and vision requests to OpenAI-compatible providers bypass `max_active_requests`.
- The web UI "Waiting X / Y in queue" message is always `0 / 0`.
- Binding providers have no queue-aware resolution; `CreateSimpleCompletion` treats them as concrete providers.

---

## 2. High-Level Architecture

### 2.1 Core Principle
> **Every HTTP request to a concrete provider goes through exactly one gate.**

- The gate is a *per-concrete-provider* counting semaphore + FIFO wait list.
- A concrete provider is defined as any provider whose `provider_type` is **not** `binding_provider`.
- Binding providers are virtual routers; they resolve to a concrete target and the target's gate is used.

### 2.2 Gate Location
- Lives **inside `OpenAIClient`** as a static singleton manager (`ProviderRequestGate`).
- No global variables outside `OpenAIClient`; completely self-contained.
- Thread-safe; uses `std::mutex`, `std::condition_variable`, and `std::atomic<int>` for counters.

### 2.3 Gate Lifecycle
1. **Acquire** — check `max_queue_size`. If queue depth would exceed, return an error (`"Provider queue full"`) rather than blocking forever (deadlock safety).
2. **Wait** — if active requests < `max_active_requests`, proceed immediately. Otherwise, enqueue in FIFO wait list and block on `cv`.
3. **Execute** — run the actual WinHTTP request.
4. **Release** — decrement active, notify next waiter, fire `on_queue_status` with updated depth.

### 2.4 Reporting
- `on_queue_status` is called **before** waiting (`state = "queued"`), **when** active (`state = "active"`), and **after** completion (`state = "done"`).
- Position and depth reflect the *provider-level* queue, not model-level. Model-level slots are enforced inside the provider gate (see §3.4).

---

## 3. Phase 1: Core Provider Queue System

### 3.1 New Fields in `ModelConfig`
```cpp
struct ModelConfig {
    // ... existing fields ...
    int max_active_requests = 0;   // 0 = inherit from provider
};
```

- **Provider-level** fields remain on `ProviderConfig`.
- **Model-level** `max_active_requests` is additive: the effective limit for a concrete call is:
  > `effective = provider.max_active_requests == 0 ? model.max_active_requests : (model.max_active_requests == 0 ? provider.max_active_requests : std::min(provider.max_active_requests, model.max_active_requests));`
  > `effective == 0` means **no limit**.

### 3.2 Persistence (`storage.cpp`)
Serialize / deserialize `model.max_active_requests` in provider JSON:
```json
{
  "models": [
    {
      "id": "gpt-4o",
      "max_active_requests": 2
    }
  ]
}
```

### 3.3 UI — Provider Manager Model Editor
- Add one numeric edit field under the model editor: **"Max concurrent requests (0 = provider default)"**.
- Only visible for concrete provider types, not for binding models.

### 3.4 `ProviderRequestGate` (new, inside `openai_client.h`)
```cpp
struct GateKey {
    std::string provider_id;
    std::string model_id;
    bool operator==(const GateKey&) const = default;
};

struct GateKeyHash {
    size_t operator()(const GateKey& k) const noexcept;
};

struct GateState {
    int max_active = 0;          // 0 = unlimited
    int currently_active = 0;
    size_t max_queue = 0;        // 0 = unlimited
    std::deque<std::thread::id> waiters;
    std::mutex mtx;
    std::condition_variable cv;
};

class ProviderRequestGate {
public:
    // Called once when provider cache is set/reloaded
    static void Configure(const std::vector<ProviderConfig>& providers);

    // Returns true if slot was acquired immediately or after waiting.
    // Calls on_status while queued.
    static bool Acquire(const GateKey& key,
                        const std::function<void(const ProviderQueueStatus&)>& on_status);

    static void Release(const GateKey& key);
};
```

**Implementation rules:**
- `Configure` rebuilds the internal map from the current provider cache. Keys that disappear are cleaned up lazily on Release.
- `Acquire` is blocking. It first checks `max_queue`; if `waiters.size() >= max_queue` and `max_queue > 0`, return `false`.
- While waiting, it periodically invokes `on_status` with `state = "queued"`, `queue_position`, `queue_depth`.
- `Release` always wakes exactly one waiter.

### 3.5 Integration Points in `OpenAIClient`
All four public request methods are updated:

```cpp
ChatExecutionResult OpenAIClient::StreamChat(
    const ChatRequestOptions& request,
    const std::function<void(const std::string&)>& on_delta,
    const std::function<void(const ProviderQueueStatus&)>& on_queue_status,
    const std::function<void(const std::string&, const std::string&)>& on_activity_status);
```

New wrapper internally:
```cpp
ChatExecutionResult OpenAIClient::RunThroughGate(
    const ChatRequestOptions& request,
    const std::function<void(const ProviderQueueStatus&)>& on_status,
    std::function<ChatExecutionResult()> do_request);
```

Every method wraps its body in `RunThroughGate`. The gate is bypassed only when `max_active == 0 && max_queue == 0`.

### 3.6 Image Ingest Fix
`DescribeImageWithProviderQueue` in `rag_service.cpp` already calls `OpenAIClient::CreateSimpleCompletion`. Once Phase 1 is in place, image ingest will **automatically** be gated without further changes.

---

## 4. Phase 2: Binding Provider Resolution

### 4.1 Problem
A binding provider is a virtual router. If we gated the binding provider itself, all its targets would share one bottleneck — wrong. Each target may have its own capacity.

### 4.2 Solution
> **Binding providers never own a gate.** Before gating, resolve the binding target. Gate the *resolved* concrete provider+model.

### 4.3 New Flow in `OpenAIClient`
```cpp
ChatRequestOptions ResolveBindingIfNeeded(const ChatRequestOptions& request) {
    if (!IsBindingProvider(request.provider)) return request;

    auto [resolved_provider, resolved_model] = binding::ResolveTarget(request);
    ChatRequestOptions out = request;
    out.provider = resolved_provider;
    out.model    = resolved_model;
    return out;
}
```

- This resolution happens **inside** `RunThroughGate`, before the acquire step.
- The gate key becomes `(resolved_provider.id, resolved_model.id)`.
- UI callbacks report the resolved provider's name to the user.

### 4.4 Cooldown & Retry on Binding Targets
When a concrete target fails with 429 / 503, the binding resolver should mark the target as "cooldown" for `busy_retry_interval_seconds`.
- This already exists in `remote_provider_worker.cpp` (`ResolveBindingUpstream`) but **only in the standalone remote worker**, not in the in-process client.
- We should share the cooldown logic by extracting `binding::ResolveTarget` into a shared header or by reusing the existing `WorkerBindingState` pattern.

**Decision:** Create `binding_resolver.h` / `binding_resolver.cpp` containing the shared round-robin + cooldown logic. Use it from both:
1. The standalone `remote_provider_worker`
2. The in-process `OpenAIClient` gated wrapper

### 4.5 Anti-Nesting Rule
> **Nested binding resolution is limited to exactly 1 level.**

If a binding target points to another binding model, the resolver stops and returns an error (`"Nested binding is not supported"`). This prevents runaway recursion and simplifies queue semantics.

---

## 5. Phase 3: Remote HTTPS Agent Protocol

### 5.1 Problem
When the remote provider worker serves models, it today proxies directly to the upstream with no queue. If 50 requests arrive, 50 upstream connections are opened.

### 5.2 Two Modes of Remote Deployment
| Scenario | Who owns the queue? |
|----------|---------------------|
| **Standalone remote model** (not part of a local binding) | Remote HTTPS Agent |
| **Remote model inside a local binding** | Local binding gate |

### 5.3 Protocol Extension: Model-Level Queue Metadata
The remote worker's `/health` endpoint (or new `/v1/worker/queue` endpoint) should expose per-model settings:

```json
{
  "worker_name": "gpu-server-1",
  "queue_managed_by": "remote",
  "models": [
    {
      "id": "llama3:70b",
      "max_active_requests": 2,
      "max_queue_size": 10,
      "self_managed_queue": true
    }
  ]
}
```

- `self_managed_queue = true` means the remote process handles its own gate. The local client bypasses the local gate for this model.
- `self_managed_queue = false` means the local client still gates.

### 5.4 Remote Worker Implementation
Inside `RunRemoteProviderWorker`, add a `ProviderRequestGate` identical to the in-process one. Wrap every `/v1/chat/completions` and `/api/chat` handler in the gate before proxying upstream.

### 5.5 Local Client Bypass Detection
When `OpenAIClient::RunThroughGate` sees a provider whose `provider_type == "agent_https"`, it should:
1. Check if the cached model metadata says `self_managed_queue == true`.
2. If yes, bypass the local gate and send the request directly.
3. If no (or metadata is stale), fall back to local gating.

**Note:** The `TestConnection` flow should also fetch the remote queue metadata and cache it with the provider.

### 5.6 Binding + Remote Edge Case
If a local binding model points to a remote model that says `self_managed_queue = true`, the local binding still resolves to that remote model. Because the remote is self-managed, the local client bypasses the gate on the final hop. The remote's gate becomes the single point of truth.

If a local binding model points to a remote model that says `self_managed_queue = false`, the local client applies its gate. This is useful when the remote is a simple passthrough worker and the local app should still throttle.

---

## 6. Phase 4: Ollama Model Tuning & Idle-Unload Prevention

### 6.1 New Fields in `ModelConfig`
```cpp
struct ModelConfig {
    // ... existing ...
    int ollama_num_parallel = 0;        // OLLAMA_NUM_PARALLEL equivalent (0 = auto)
    int ollama_max_loaded_models = 0;   // OLLAMA_MAX_LOADED_MODELS equivalent (0 = auto)
};
```

- `ollama_keep_alive_seconds` already exists. `-1` should mean "keep loaded forever".
- `ollama_num_parallel` controls how many requests a single Ollama model can service at once. This should map directly to the model's `max_active_requests` if the user leaves it at `0` (auto).

### 6.2 UI — Provider Manager Model Editor (Ollama tab)
Add two new fields under the existing Ollama controls:
- **Parallel request slots (0 = auto):** maps to `ollama_num_parallel` and defaults to model `max_active_requests`.
- **Max loaded models (0 = auto):** maps to `ollama_max_loaded_models`.

### 6.3 Idle-Unload Prevention While Queued
When `ollama_keep_alive_seconds > 0` and a model has queued waiters, we must prevent Ollama from unloading it. The `ProviderRequestGate` already knows how many waiters exist for a given model.

**Approach (best-effort):**
- During the wait phase, the gate thread can send a lightweight Ollama `/api/generate` with `keep_alive` set to a high value. However, this is expensive.
- **Simpler approach:** When `ollama_keep_alive_seconds > 0`, the `EnsureOllamaLocalRunning` helper already preloads the model if needed. We do not need active pinging; just ensure the first request in the queue triggers the preload, and subsequent requests benefit because the model stays loaded while any request is in flight. Ollama's own in-flight requests naturally keep the model loaded.

### 6.4 Relationship to Max Active Requests
For Ollama local providers, `max_active_requests` at provider or model level should default to `ollama_num_parallel` if the user does not explicitly set it. When the user sets `ollama_num_parallel = 4` but leaves `max_active_requests = 0`, the effective limit is `4`.

---

## 7. Data Model Changes Summary

### 7.1 `types.h`
**`ModelConfig`** — add:
```cpp
int max_active_requests = 0;        // 0 = use provider default or Ollama auto
int ollama_num_parallel = 0;      // OLLAMA_NUM_PARALLEL equivalent
int ollama_max_loaded_models = 0; // OLLAMA_MAX_LOADED_MODELS equivalent
```

### 7.2 `ProviderConfig`
No new fields. `max_active_requests` and `max_queue_size` remain.

### 7.3 `ProviderQueueStatus`
No new fields. Values will finally be populated.

### 7.4 `storage.cpp` (JSON)
Add to model serialization block:
```cpp
{"max_active_requests", m.max_active_requests},
{"ollama_num_parallel", m.ollama_num_parallel},
{"ollama_max_loaded_models", m.ollama_max_loaded_models},
```

### 7.5 Remote Worker Config (`remote_provider_worker.h`)
Add to `RemoteProviderWorkerExportedProvider` or extend `/health` to include queue metadata per model.

---

## 8. Backward Compatibility

| Item | Compatibility |
|------|--------------|
| `provider.max_active_requests` existing JSON | Fully preserved; new code reads it |
| `model.max_active_requests` missing in old JSON | Defaults to `0`, meaning "inherit from provider / auto" |
| `on_queue_status` callbacks existing in codebase | Will finally receive real data |
| Remote worker `/health` missing queue metadata | Local client falls back to `self_managed_queue = false` (local gate) |
| Existing `ImageVisionWorkerQueue` for Ollama | Unchanged; only the **Pick Provider** path changes |

---

## 9. Testing Strategy

1. **Unit:** Mock `ProviderRequestGate` with synthetic delays. Verify FIFO ordering, max_queue rejection, and status callback accuracy.
2. **Integration:** Connect to a dummy httplib server that adds 2-second artificial latency.
   - Fire 10 simultaneous chat completions against a provider with `max_active_requests = 3`.
   - Verify only 3 are active at any moment.
   - Verify 7 are queued and receive correct `queue_position`.
3. **Binding:** Configure a binding provider with two targets, each limited to 1 slot. Send 4 requests. Verify round-robin alternation and that no target exceeds 1 active request.
4. **Remote:** Run a remote worker with `self_managed_queue = true` and max_active = 2. Fire 5 requests. Verify that the remote's gate limits to 2 and the local client reports 0 active (because it bypasses).
5. **Ollama:** Start Ollama with a vision model. Set `ollama_num_parallel = 2`, `ollama_keep_alive_seconds = -1`. Send 5 image ingests. Verify 2 in flight, 3 queued.

---

## 10. Files to Touch (expected)

| File | Change |
|------|--------|
| `src/types.h` | Add model fields |
| `src/storage.cpp` | Persist model fields |
| `src/provider_manager.cpp` | New model editor fields |
| `src/openai_client.h` | New `ProviderRequestGate`, `binding::ResolveTarget` forward decl |
| `src/openai_client.cpp` | Implement gate; wrap all request methods |
| `src/binding_resolver.h` *(new)* | Extracted shared binding resolution |
| `src/binding_resolver.cpp` *(new)* | Shared round-robin + cooldown logic |
| `src/remote_provider_worker.cpp` | Add `/health` queue metadata; wrap POST in gate |
| `src/remote_provider_worker.h` | Add queue fields to exported provider |
| `src/ollama_api_client.cpp` / `.h` | Wire gate into local Ollama calls (Phase 3+) |
| `src/rag_service.cpp` | No change needed for Phase 1 (calls `CreateSimpleCompletion`) |
| `src/rag_service_manager.cpp` | Update image ingest status message to reflect real queue depth |
| `src/web_server.cpp` | Already receives `ProviderQueueStatus`; no changes unless binding resolution timing needs exposing |

---

## 11. Open Questions

1. **Should the gate use `std::counting_semaphore` (C++20) or a `std::mutex` + `cv` based manual counter?** The team currently uses `std::mutex` everywhere. Manual counter is safer for cross-compiler consistency.
2. **Should model-level `max_queue_size` exist in addition to provider-level?** Proposal: no. Queue size is a provider-level capacity concern. Only `max_active_requests` is model-level. This simplifies UX and avoids "partially full" ambiguous states.
3. **Should queued requests be cancellable?** The existing `ActiveStreamCancellation` in `web_server.cpp` only covers HTTP streams. For Phase 1, queued requests are not cancellable. This can be added later with a cancellation token passed through `Acquire`.
4. **Should the remote worker support a queue query endpoint (`GET /queue`) for diagnostics?** Useful but not critical for Phase 1-3. Defer to Phase 4 or a separate feature.

---

**Author:** OpenCode Agent  
**Date:** 2026-05-02  
**Status:** Draft — awaiting review before implementation
