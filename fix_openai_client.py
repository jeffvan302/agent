import os

def read_file(path):
    with open(path, 'rb') as f:
        raw = f.read()
    for enc in ['utf-8-sig', 'utf-8', 'utf-16-le', 'utf-16-be']:
        try:
            return raw.decode(enc), enc
        except Exception:
            continue
    raise RuntimeError('Cannot decode ' + path)

def write_file(path, text, enc):
    with open(path, 'wb') as f:
        f.write(text.encode(enc))

# ===================== openai_client.cpp =====================
raw_text, raw_enc = read_file('src/openai_client.cpp')

# Ensure #include <unordered_map>
if '#include <unordered_map>' not in raw_text:
    raw_text = raw_text.replace('#include <thread>', '#include <thread>\n#include <unordered_map>')

# --- Insert ResolveBindingTarget after GateSlot::~GateSlot() ---
old = (
    "GateSlot::~GateSlot() {\n"
    "    if (acquired) {\n"
    "        ProviderRequestGate::Release(key);\n"
    "    }\n"
    "}\n"
    "\n"
    "namespace {\n"
    "struct InternetHandleCloser {"
)
new = (
    "GateSlot::~GateSlot() {\n"
    "    if (acquired) {\n"
    "        ProviderRequestGate::Release(key);\n"
    "    }\n"
    "}\n"
    "\n"
    "bool ResolveBindingTarget(const std::vector<ProviderConfig>& providers,\n"
    "                          const ModelConfig& binding_model,\n"
    "                          const ChatRequestOptions& original_request,\n"
    "                          ChatRequestOptions* out_request,\n"
    "                          const std::function<void(const ProviderQueueStatus&)>& on_queue_status) {\n"
    "    if (!binding_model.is_binding_model || binding_model.binding_targets.empty()) return false;\n"
    "    const bool round_robin = binding_model.binding_routing_mode == BindingRoutingMode::RoundRobin;\n"
    "    auto is_cooldown = [&](const BindingTargetConfig& t) -> bool {\n"
    "        return OpenAIClient::BindingCooldown(t.provider_id, t.model_id);\n"
    "    };\n"
    "    std::vector<const BindingTargetConfig*> candidates;\n"
    "    for (const auto& t : binding_model.binding_targets) {\n"
    "        if (!is_cooldown(t)) candidates.push_back(&t);\n"
    "    }\n"
    "    if (candidates.empty()) {\n"
    "        for (const auto& t : binding_model.binding_targets) {\n"
    "            candidates.push_back(&t);\n"
    "        }\n"
    "    }\n"
    "    const BindingTargetConfig* selected = nullptr;\n"
    "    if (round_robin) {\n"
    "        static std::unordered_map<std::string, size_t> rr_index;\n"
    "        static std::mutex rr_mtx;\n"
    "        std::lock_guard<std::mutex> lock(rr_mtx);\n"
    "        size_t& idx = rr_index[binding_model.id];\n"
    "        for (size_t attempt = 0; attempt < candidates.size(); ++attempt) {\n"
    "            const auto* candidate = candidates[idx % candidates.size()];\n"
    "            idx++;\n"
    "            int eff_active = 1, eff_queue = 100;\n"
    "            bool self_managed = false;\n"
    "            {\n"
    "                std::unique_lock<std::mutex> map_lock(GateMapMutex());\n"
    "                auto it = s_gate_provider_cache.find(candidate->provider_id);\n"
    "                if (it != s_gate_provider_cache.end()) {\n"
    "                    int p_active = it->second.max_active_requests;\n"
    "                    int p_queue = it->second.max_queue_size;\n"
    "                    int m_active = 0, m_queue = 0;\n"
    "                    for (const auto& m : it->second.models) {\n"
    "                        if (m.id == candidate->model_id) {\n"
    "                            m_active = m.max_active_requests;\n"
    "                            m_queue = m.max_queue_size;\n"
    "                            self_managed = m.self_managed_queue;\n"
    "                            break;\n"
    "                        }\n"
    "                    }\n"
    "                    eff_active = ComputeEffectiveMaxActive(p_active, m_active);\n"
    "                    eff_queue = ComputeEffectiveMaxQueue(p_queue, m_queue);\n"
    "                }\n"
    "            }\n"
    "            if (self_managed) { selected = candidate; break; }\n"
    "            if (ProviderRequestGate::Acquire({candidate->provider_id, candidate->model_id, GateDomain::Chat}, eff_active, eff_queue, on_queue_status)) {\n"
    "                selected = candidate;\n"
    "                break;\n"
    "            }\n"
    "        }\n"
    "    } else {\n"
    "        for (const auto* candidate : candidates) {\n"
    "            int eff_active = 1, eff_queue = 100;\n"
    "            bool self_managed = false;\n"
    "            {\n"
    "                std::unique_lock<std::mutex> map_lock(GateMapMutex());\n"
    "                auto it = s_gate_provider_cache.find(candidate->provider_id);\n"
    "                if (it != s_gate_provider_cache.end()) {\n"
    "                    int p_active = it->second.max_active_requests;\n"
    "                    int p_queue = it->second.max_queue_size;\n"
    "                    int m_active = 0, m_queue = 0;\n"
    "                    for (const auto& m : it->second.models) {\n"
    "                        if (m.id == candidate->model_id) {\n"
    "                            m_active = m.max_active_requests;\n"
    "                            m_queue = m.max_queue_size;\n"
    "                            self_managed = m.self_managed_queue;\n"
    "                            break;\n"
    "                        }\n"
    "                    }\n"
    "                    eff_active = ComputeEffectiveMaxActive(p_active, m_active);\n"
    "                    eff_queue = ComputeEffectiveMaxQueue(p_queue, m_queue);\n"
    "                }\n"
    "            }\n"
    "            if (self_managed) { selected = candidate; break; }\n"
    "            if (ProviderRequestGate::Acquire({candidate->provider_id, candidate->model_id, GateDomain::Chat}, eff_active, eff_queue, on_queue_status)) {\n"
    "                selected = candidate;\n"
    "                break;\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    if (!selected) return false;\n"
    "    for (const auto& p : providers) {\n"
    "        if (p.id != selected->provider_id) continue;\n"
    "        for (const auto& m : p.models) {\n"
    "            if (m.id == selected->model_id) {\n"
    "                out_request->provider = p;\n"
    "                out_request->model = m;\n"
    "                out_request->system_prompt = original_request.system_prompt;\n"
    "                out_request->temperature = original_request.temperature;\n"
    "                out_request->max_tokens = original_request.max_tokens;\n"
    "                out_request->messages = original_request.messages;\n"
    "                out_request->binding_model_id = binding_model.id;\n"
    "                out_request->binding_depth = original_request.binding_depth + 1;\n"
    "                return true;\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    return false;\n"
    "}\n"
    "\n"
    "namespace {\n"
    "struct InternetHandleCloser {"
)

if old in raw_text:
    raw_text = raw_text.replace(old, new)
else:
    print('WARN: insertion point old not found in openai_client.cpp')

# --- binding bypass blocks ---
bypass_intro = (
    "    // Binding bypass: resolve to concrete target and acquire its gate directly\n"
    "    if (request.model.is_binding_model && request.model.bypass_queue && request.binding_depth < 8) {\n"
    "        ChatRequestOptions resolved = request;\n"
    "        std::vector<ProviderConfig> all_providers;\n"
    "        {\n"
    "            std::lock_guard<std::mutex> lock(GateMapMutex());\n"
    "            for (const auto& kv : s_gate_provider_cache) all_providers.push_back(kv.second);\n"
    "        }\n"
    "        if (ResolveBindingTarget(all_providers, request.model, request, &resolved, on_queue_status)) {\n"
)

# StreamChat
old_sc = (
    "ChatExecutionResult OpenAIClient::StreamChat(const ChatRequestOptions& request,\n"
    "                                           const std::function<void(const std::string&)>& on_delta,\n"
    "                                           const std::function<void(const ProviderQueueStatus&)>& on_queue_status,\n"
    "                                           const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {\n"
    "    GateSlot slot(request, GateDomain::Chat);\n"
    "    if (!slot.Acquire(request.provider.name, on_queue_status)) {\n"
    "        ChatExecutionResult result;\n"
    "        result.error = \"Provider queue is full for \" + request.provider.name + \"/\" + request.model.id + \".\";\n"
    "        return result;\n"
    "    }\n"
    "    try {"
)
new_sc = (
    "ChatExecutionResult OpenAIClient::StreamChat(const ChatRequestOptions& request,\n"
    "                                           const std::function<void(const std::string&)>& on_delta,\n"
    "                                           const std::function<void(const ProviderQueueStatus&)>& on_queue_status,\n"
    "                                           const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {\n"
    + bypass_intro +
    "            return StreamChat(resolved, on_delta, on_queue_status, {});\n"
    "        }\n"
    "        ChatExecutionResult fail;\n"
    "        fail.error = \"Binding resolution failed: no target available for \" + request.model.id;\n"
    "        return fail;\n"
    "    }\n"
    "    GateSlot slot(request, GateDomain::Chat);\n"
    "    if (!slot.Acquire(request.provider.name, on_queue_status)) {\n"
    "        ChatExecutionResult result;\n"
    "        result.error = \"Provider queue is full for \" + request.provider.name + \"/\" + request.model.id + \".\";\n"
    "        return result;\n"
    "    }\n"
    "    try {"
)
if old_sc in raw_text:
    raw_text = raw_text.replace(old_sc, new_sc)
else:
    print('WARN: StreamChat old not found')

# CreateToolAwareCompletion
old_cta = (
    "ChatCompletionResult OpenAIClient::CreateToolAwareCompletion(const ChatRequestOptions& request,\n"
    "                                                           const std::vector<ChatToolDefinition>& tools,\n"
    "                                                           const std::function<void(const ProviderQueueStatus&)>& on_queue_status,\n"
    "                                                           const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {\n"
    "    GateSlot slot(request, GateDomain::Chat);\n"
    "    if (!slot.Acquire(request.provider.name, on_queue_status)) {\n"
    "        ChatCompletionResult result;\n"
    "        result.error = \"Provider queue is full for \" + request.provider.name + \"/\" + request.model.id + \".\";\n"
    "        return result;\n"
    "    }\n"
    "    if (IsOllamaLocalProvider(request.provider)) {"
)
new_cta = (
    "ChatCompletionResult OpenAIClient::CreateToolAwareCompletion(const ChatRequestOptions& request,\n"
    "                                                           const std::vector<ChatToolDefinition>& tools,\n"
    "                                                           const std::function<void(const ProviderQueueStatus&)>& on_queue_status,\n"
    "                                                           const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {\n"
    + bypass_intro +
    "            return CreateToolAwareCompletion(resolved, tools, on_queue_status, {});\n"
    "        }\n"
    "        ChatCompletionResult fail;\n"
    "        fail.error = \"Binding resolution failed: no target available for \" + request.model.id;\n"
    "        return fail;\n"
    "    }\n"
    "    GateSlot slot(request, GateDomain::Chat);\n"
    "    if (!slot.Acquire(request.provider.name, on_queue_status)) {\n"
    "        ChatCompletionResult result;\n"
    "        result.error = \"Provider queue is full for \" + request.provider.name + \"/\" + request.model.id + \".\";\n"
    "        return result;\n"
    "    }\n"
    "    if (IsOllamaLocalProvider(request.provider)) {"
)
if old_cta in raw_text:
    raw_text = raw_text.replace(old_cta, new_cta)
else:
    print('WARN: CreateToolAwareCompletion old not found')

# CreateSimpleCompletion
old_csc = (
    "ChatCompletionResult OpenAIClient::CreateSimpleCompletion(const ChatRequestOptions& request,\n"
    "                                                        const std::function<void(const ProviderQueueStatus&)>& on_queue_status,\n"
    "                                                        const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {\n"
    "    GateSlot slot(request, GateDomain::Chat);\n"
    "    if (!slot.Acquire(request.provider.name, on_queue_status)) {\n"
    "        ChatCompletionResult result;\n"
    "        result.error = \"Provider queue is full for \" + request.provider.name + \"/\" + request.model.id + \".\";\n"
    "        return result;\n"
    "    }\n"
    "    if (IsOllamaLocalProvider(request.provider)) {"
)
new_csc = (
    "ChatCompletionResult OpenAIClient::CreateSimpleCompletion(const ChatRequestOptions& request,\n"
    "                                                        const std::function<void(const ProviderQueueStatus&)>& on_queue_status,\n"
    "                                                        const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {\n"
    + bypass_intro +
    "            return CreateSimpleCompletion(resolved, on_queue_status, {});\n"
    "        }\n"
    "        ChatCompletionResult fail;\n"
    "        fail.error = \"Binding resolution failed: no target available for \" + request.model.id;\n"
    "        return fail;\n"
    "    }\n"
    "    GateSlot slot(request, GateDomain::Chat);\n"
    "    if (!slot.Acquire(request.provider.name, on_queue_status)) {\n"
    "        ChatCompletionResult result;\n"
    "        result.error = \"Provider queue is full for \" + request.provider.name + \"/\" + request.model.id + \".\";\n"
    "        return result;\n"
    "    }\n"
    "    if (IsOllamaLocalProvider(request.provider)) {"
)
if old_csc in raw_text:
    raw_text = raw_text.replace(old_csc, new_csc)
else:
    print('WARN: CreateSimpleCompletion old not found')

# StreamToolAwareCompletion
old_stc = (
    "ChatCompletionResult OpenAIClient::StreamToolAwareCompletion(const ChatRequestOptions& request,\n"
    "                                                             const std::vector<ChatToolDefinition>& tools,\n"
    "                                                             const std::function<void(const std::string&)>& on_delta,\n"
    "                                                             const std::function<void(const ProviderQueueStatus&)>& on_queue_status,\n"
    "                                                             const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {\n"
    "    GateSlot slot(request, GateDomain::Chat);\n"
    "    if (!slot.Acquire(request.provider.name, on_queue_status)) {\n"
    "        ChatCompletionResult result;\n"
    "        result.error = \"Provider queue is full for \" + request.provider.name + \"/\" + request.model.id + \".\";\n"
    "        return result;\n"
    "    }\n"
    "    if (IsOllamaLocalProvider(request.provider)) {"
)
new_stc = (
    "ChatCompletionResult OpenAIClient::StreamToolAwareCompletion(const ChatRequestOptions& request,\n"
    "                                                             const std::vector<ChatToolDefinition>& tools,\n"
    "                                                             const std::function<void(const std::string&)>& on_delta,\n"
    "                                                             const std::function<void(const ProviderQueueStatus&)>& on_queue_status,\n"
    "                                                             const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {\n"
    + bypass_intro +
    "            return StreamToolAwareCompletion(resolved, tools, on_delta, on_queue_status, {});\n"
    "        }\n"
    "        ChatCompletionResult fail;\n"
    "        fail.error = \"Binding resolution failed: no target available for \" + request.model.id;\n"
    "        return fail;\n"
    "    }\n"
    "    GateSlot slot(request, GateDomain::Chat);\n"
    "    if (!slot.Acquire(request.provider.name, on_queue_status)) {\n"
    "        ChatCompletionResult result;\n"
    "        result.error = \"Provider queue is full for \" + request.provider.name + \"/\" + request.model.id + \".\";\n"
    "        return result;\n"
    "    }\n"
    "    if (IsOllamaLocalProvider(request.provider)) {"
)
if old_stc in raw_text:
    raw_text = raw_text.replace(old_stc, new_stc)
else:
    print('WARN: StreamToolAwareCompletion old not found')

# --- CreateEmbedding rewrite ---
old_embed = (
    "std::vector<std::vector<float>> OpenAIClient::CreateEmbedding(const ProviderConfig& provider,\n"
    "                                                              const ModelConfig& model,\n"
    "                                                              const std::vector<std::string>& texts,\n"
    "                                                              const std::function<void(const ProviderQueueStatus&)>& on_queue_status) {\n"
    "    if (texts.empty()) return {};\n"
    "\n"
    "    // Build a minimal ChatRequestOptions for gate acquisition (embedding domain)\n"
    "    ChatRequestOptions gate_request;\n"
    "    gate_request.provider = provider;\n"
    "    gate_request.model = model;\n"
    "\n"
    "    GateSlot slot(gate_request, GateDomain::Embedding);\n"
    "    if (!slot.Acquire(provider.name, on_queue_status)) {\n"
    "        throw std::runtime_error(\"Provider queue is full for embedding: \" + provider.name + \"/\" + model.id + \".\");\n"
    "    }\n"
    "\n"
    "    // Embedding implementations are currently provided by rag_service.cpp providers.\n"
    "    // This stub avoids duplicating HTTP/JSON helpers; wire here when needed.\n"
    "    (void)provider;\n"
    "    (void)model;\n"
    "    throw std::runtime_error(\"CreateEmbedding not yet implemented in OpenAIClient.\");\n"
    "}"
)
new_embed = (
    "std::vector<std::vector<float>> OpenAIClient::CreateEmbedding(const ProviderConfig& provider,\n"
    "                                                              const ModelConfig& model,\n"
    "                                                              const std::vector<std::string>& texts,\n"
    "                                                              const std::function<void(const ProviderQueueStatus&)>& on_queue_status) {\n"
    "    if (texts.empty()) return {};\n"
    "    if (model.is_binding_model && model.bypass_queue) {\n"
    "        ChatRequestOptions synthetic; synthetic.provider = provider; synthetic.model = model;\n"
    "        ChatRequestOptions resolved = synthetic;\n"
    "        std::vector<ProviderConfig> all_providers;\n"
    "        { std::lock_guard<std::mutex> lock(GateMapMutex()); for (const auto& kv : s_gate_provider_cache) all_providers.push_back(kv.second); }\n"
    "        if (!ResolveBindingTarget(all_providers, model, synthetic, &resolved, on_queue_status)) {\n"
    "            throw std::runtime_error(\"Embedding binding resolution failed for \" + model.id);\n"
    "        }\n"
    "        return CreateEmbedding(resolved.provider, resolved.model, texts, on_queue_status);\n"
    "    }\n"
    "    ChatRequestOptions gate_request;\n"
    "    gate_request.provider = provider;\n"
    "    gate_request.model = model;\n"
    "    GateSlot slot(gate_request, GateDomain::Embedding);\n"
    "    if (!slot.Acquire(provider.name, on_queue_status)) {\n"
    "        throw std::runtime_error(\"Provider queue is full for embedding: \" + provider.name + \"/\" + model.id + \".\");\n"
    "    }\n"
    "    const std::string ptype = NormalizeProviderType(provider.provider_type);\n"
    "    if (ptype == \"ollama_local\") {\n"
    "        return RunOllamaLocalEmbedding(provider, model, texts, on_queue_status);\n"
    "    }\n"
    "    if (ptype == \"ollama_cloud\") {\n"
    "        return RunOllamaCloudEmbedding(provider, model, texts, on_queue_status);\n"
    "    }\n"
    "    return RunOpenAICompatibleEmbedding(provider, model, texts, on_queue_status);\n"
    "}"
    "\n"
    "std::vector<std::vector<float>> RunOllamaLocalEmbedding(const ProviderConfig& provider,\n"
    "                                                         const ModelConfig& model,\n"
    "                                                         const std::vector<std::string>& texts,\n"
    "                                                         const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/) {\n"
    "    (void)provider; (void)model; (void)texts;\n"
    "    throw std::runtime_error(\"RunOllamaLocalEmbedding not yet implemented.\");\n"
    "}\n"
    "\n"
    "std::vector<std::vector<float>> RunOllamaCloudEmbedding(const ProviderConfig& provider,\n"
    "                                                         const ModelConfig& model,\n"
    "                                                         const std::vector<std::string>& texts,\n"
    "                                                         const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/) {\n"
    "    (void)provider; (void)model; (void)texts;\n"
    "    throw std::runtime_error(\"RunOllamaCloudEmbedding not yet implemented.\");\n"
    "}\n"
    "\n"
    "std::vector<std::vector<float>> RunOpenAICompatibleEmbedding(const ProviderConfig& provider,\n"
    "                                                             const ModelConfig& model,\n"
    "                                                             const std::vector<std::string>& texts,\n"
    "                                                             const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/) {\n"
    "    (void)provider; (void)model; (void)texts;\n"
    "    throw std::runtime_error(\"RunOpenAICompatibleEmbedding not yet implemented.\");\n"
    "}"
)

if old_embed in raw_text:
    raw_text = raw_text.replace(old_embed, new_embed)
    print('Rewrote CreateEmbedding + added stubs')
else:
    print('WARN: old CreateEmbedding body not found')

write_file('src/openai_client.cpp', raw_text, raw_enc)
print('Done openai_client.cpp')

# ===================== openai_client.h =====================
h_text, h_enc = read_file('src/openai_client.h')

old_class = 'class OpenAIClient {'
new_class = (
    'struct BindingTargetConfig;\n'
    'enum class BindingRoutingMode;\n'
    '\n'
    'bool ResolveBindingTarget(const std::vector<ProviderConfig>& providers,\n'
    '                          const ModelConfig& binding_model,\n'
    '                          const ChatRequestOptions& original_request,\n'
    '                          ChatRequestOptions* out_request,\n'
    '                          const std::function<void(const ProviderQueueStatus&)>& on_queue_status);\n'
    '\n'
    'std::vector<std::vector<float>> RunOllamaLocalEmbedding(const ProviderConfig& provider,\n'
    '                                                         const ModelConfig& model,\n'
    '                                                         const std::vector<std::string>& texts,\n'
    '                                                         const std::function<void(const ProviderQueueStatus&)>& on_queue_status);\n'
    '\n'
    'std::vector<std::vector<float>> RunOllamaCloudEmbedding(const ProviderConfig& provider,\n'
    '                                                         const ModelConfig& model,\n'
    '                                                         const std::vector<std::string>& texts,\n'
    '                                                         const std::function<void(const ProviderQueueStatus&)>& on_queue_status);\n'
    '\n'
    'std::vector<std::vector<float>> RunOpenAICompatibleEmbedding(const ProviderConfig& provider,\n'
    '                                                             const ModelConfig& model,\n'
    '                                                             const std::vector<std::string>& texts,\n'
    '                                                             const std::function<void(const ProviderQueueStatus&)>& on_queue_status);\n'
    '\n'
    'class OpenAIClient {'
)

if old_class in h_text:
    h_text = h_text.replace(old_class, new_class)
    print('Updated openai_client.h')
else:
    print('WARN: class OpenAIClient not found in header')

write_file('src/openai_client.h', h_text, h_enc)
print('All edits complete.')
