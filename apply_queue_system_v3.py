import os

def read_lines(path):
    with open(path, 'rb') as f:
        raw = f.read()
    for enc in ['utf-8-sig', 'utf-8', 'utf-16-le', 'utf-16-be']:
        try:
            text = raw.decode(enc)
            return text.splitlines(True), enc
        except Exception:
            continue
    raise RuntimeError('Cannot decode ' + path)

def write_lines(path, lines, enc):
    with open(path, 'wb') as f:
        f.write(''.join(lines).encode(enc))

# ---------- openai_client.cpp ----------
lines, enc = read_lines('src/openai_client.cpp')

# Remove any previous mis-inserted junk (should be clean from HEAD)

# Step 1: Insert ResolveBindingTarget after GateSlot destructor
insert_idx = None
for i, line in enumerate(lines):
    if 'GateSlot::~GateSlot() {' in line:
        j = i + 1
        while j < len(lines):
            if lines[j].strip().startswith('}'):
                # find next non-empty line
                k = j + 1
                while k < len(lines) and lines[k].strip() == '':
                    k += 1
                insert_idx = k  # right before the next thing (namespace)
                break
            j += 1
        break

if insert_idx is not None:
    block = [
        "\n",
        "bool ResolveBindingTarget(const std::vector<ProviderConfig>& providers,\n",
        "                          const ModelConfig& binding_model,\n",
        "                          const ChatRequestOptions& original_request,\n",
        "                          ChatRequestOptions* out_request,\n",
        "                          const std::function<void(const ProviderQueueStatus&)>& on_queue_status) {\n",
        "    if (!binding_model.is_binding_model || binding_model.binding_targets.empty()) return false;\n",
        "    const bool round_robin = binding_model.binding_routing_mode == BindingRoutingMode::RoundRobin;\n",
        "    auto is_cooldown = [&](const BindingTargetConfig& t) -> bool {\n",
        "        return OpenAIClient::BindingCooldown(t.provider_id, t.model_id);\n",
        "    };\n",
        "    std::vector<const BindingTargetConfig*> candidates;\n",
        "    for (const auto& t : binding_model.binding_targets) {\n",
        "        if (!is_cooldown(t)) candidates.push_back(&t);\n",
        "    }\n",
        "    if (candidates.empty()) {\n",
        "        for (const auto& t : binding_model.binding_targets) {\n",
        "            candidates.push_back(&t);\n",
        "        }\n",
        "    }\n",
        "    const BindingTargetConfig* selected = nullptr;\n",
        "    if (round_robin) {\n",
        "        static std::unordered_map<std::string, size_t> rr_index;\n",
        "        static std::mutex rr_mtx;\n",
        "        std::lock_guard<std::mutex> lock(rr_mtx);\n",
        "        size_t& idx = rr_index[binding_model.id];\n",
        "        for (size_t attempt = 0; attempt < candidates.size(); ++attempt) {\n",
        "            const auto* candidate = candidates[idx % candidates.size()];\n",
        "            idx++;\n",
        "            int p_active = 0, p_queue = 0, m_active = 0, m_queue = 0;\n",
        "            bool self_managed = false;\n",
        "            {\n",
        "                std::unique_lock<std::mutex> map_lock(GateMapMutex());\n",
        "                auto it = s_gate_provider_cache.find(candidate->provider_id);\n",
        "                if (it != s_gate_provider_cache.end()) {\n",
        "                    p_active = it->second.max_active_requests;\n",
        "                    p_queue = it->second.max_queue_size;\n",
        "                    for (const auto& m : it->second.models) {\n",
        "                        if (m.id == candidate->model_id) {\n",
        "                            m_active = m.max_active_requests;\n",
        "                            m_queue = m.max_queue_size;\n",
        "                            self_managed = m.self_managed_queue;\n",
        "                            break;\n",
        "                        }\n",
        "                    }\n",
        "                }\n",
        "            }\n",
        "            int eff_active = ComputeEffectiveMaxActive(p_active, m_active);\n",
        "            int eff_queue = ComputeEffectiveMaxQueue(p_queue, m_queue);\n",
        "            if (self_managed) {\n",
        "                selected = candidate;\n",
        "                break;\n",
        "            }\n",
        "            GateKey gk{candidate->provider_id, candidate->model_id, GateDomain::Chat};\n",
        "            if (ProviderRequestGate::Acquire(gk, eff_active, eff_queue, on_queue_status)) {\n",
        "                selected = candidate;\n",
        "                break;\n",
        "            }\n",
        "        }\n",
        "    } else {\n",
        "        for (const auto* candidate : candidates) {\n",
        "            int p_active = 0, p_queue = 0, m_active = 0, m_queue = 0;\n",
        "            bool self_managed = false;\n",
        "            {\n",
        "                std::unique_lock<std::mutex> map_lock(GateMapMutex());\n",
        "                auto it = s_gate_provider_cache.find(candidate->provider_id);\n",
        "                if (it != s_gate_provider_cache.end()) {\n",
        "                    p_active = it->second.max_active_requests;\n",
        "                    p_queue = it->second.max_queue_size;\n",
        "                    for (const auto& m : it->second.models) {\n",
        "                        if (m.id == candidate->model_id) {\n",
        "                            m_active = m.max_active_requests;\n",
        "                            m_queue = m.max_queue_size;\n",
        "                            self_managed = m.self_managed_queue;\n",
        "                            break;\n",
        "                        }\n",
        "                    }\n",
        "                }\n",
        "            }\n",
        "            int eff_active = ComputeEffectiveMaxActive(p_active, m_active);\n",
        "            int eff_queue = ComputeEffectiveMaxQueue(p_queue, m_queue);\n",
        "            if (self_managed) {\n",
        "                selected = candidate;\n",
        "                break;\n",
        "            }\n",
        "            GateKey gk{candidate->provider_id, candidate->model_id, GateDomain::Chat};\n",
        "            if (ProviderRequestGate::Acquire(gk, eff_active, eff_queue, on_queue_status)) {\n",
        "                selected = candidate;\n",
        "                break;\n",
        "            }\n",
        "        }\n",
        "    }\n",
        "    if (!selected) return false;\n",
        "    for (const auto& p : providers) {\n",
        "        if (p.id != selected->provider_id) continue;\n",
        "        for (const auto& m : p.models) {\n",
        "            if (m.id == selected->model_id) {\n",
        "                out_request->provider = p;\n",
        "                out_request->model = m;\n",
        "                out_request->system_prompt = original_request.system_prompt;\n",
        "                out_request->temperature = original_request.temperature;\n",
        "                out_request->max_tokens = original_request.max_tokens;\n",
        "                out_request->messages = original_request.messages;\n",
        "                out_request->binding_model_id = binding_model.id;\n",
        "                out_request->binding_depth = original_request.binding_depth + 1;\n",
        "                return true;\n",
        "            }\n",
        "        }\n",
        "    }\n",
        "    return false;\n",
        "}\n",
        "\n",
    ]
    for l in reversed(block):
        lines.insert(insert_idx, l)
    print('Inserted ResolveBindingTarget')
else:
    print('WARN: insertion point not found')

# Ensure <unordered_map> is present
has_unordered_map = any('#include <unordered_map>' in l for l in lines)
if not has_unordered_map:
    for i, line in enumerate(lines):
        if '#include <thread>' in line:
            lines.insert(i + 1, '#include <unordered_map>\n')
            break

write_lines('src/openai_client.cpp', lines, enc)
print('Done openai_client.cpp step 1')

# Step 2: Add binding bypass inside each function body + CreateEmbedding rewrite
lines, enc = read_lines('src/openai_client.cpp')

bypass_block = [
    "    // Binding bypass: resolve to concrete target and acquire its gate directly\n",
    "    if (request.model.is_binding_model && request.model.bypass_queue && request.binding_depth < 8) {\n",
    "        ChatRequestOptions resolved = request;\n",
    "        std::vector<ProviderConfig> all_providers;\n",
    "        {\n",
    "            std::lock_guard<std::mutex> lock(GateMapMutex());\n",
    "            for (const auto& kv : s_gate_provider_cache) all_providers.push_back(kv.second);\n",
    "        }\n",
    "        if (ResolveBindingTarget(all_providers, request.model, request, &resolved, on_queue_status)) {\n",
]

def find_and_insert(lines, anchor, extra):
    for i, line in enumerate(lines):
        if anchor in line:
            for l in reversed(extra):
                lines.insert(i, l)
            return True
    return False

# StreamChat (returns ChatExecutionResult)
stream_extras = [
    "            return StreamChat(resolved, on_delta, on_queue_status, {});\n",
    "        }\n",
    "        ChatExecutionResult fail;\n",
    "        fail.error = \"Binding resolution failed: no target available for \" + request.model.id;\n",
    "        return fail;\n",
    "    }\n",
]
find_and_insert(lines, 'GateSlot slot(request, GateDomain::Chat);', bypass_block + stream_extras)

# CreateToolAwareCompletion (returns ChatCompletionResult) – second occurrence
found = False
for i, line in enumerate(lines):
    if 'ChatCompletionResult OpenAIClient::CreateToolAwareCompletion(const ChatRequestOptions& request,' in line:
        # search for GateSlot inside this function
        for j in range(i + 1, min(i + 20, len(lines))):
            if 'GateSlot slot(request, GateDomain::Chat);' in lines[j]:
                for l in reversed(bypass_block + [
                    "            return CreateToolAwareCompletion(resolved, tools, on_queue_status, {});\n",
                    "        }\n",
                    "        ChatCompletionResult fail;\n",
                    "        fail.error = \"Binding resolution failed: no target available for \" + request.model.id;\n",
                    "        return fail;\n",
                    "    }\n",
                ]):
                    lines.insert(j, l)
                found = True
                break
        if found:
            break

# CreateSimpleCompletion
found = False
for i, line in enumerate(lines):
    if 'ChatCompletionResult OpenAIClient::CreateSimpleCompletion(const ChatRequestOptions& request,' in line:
        for j in range(i + 1, min(i + 20, len(lines))):
            if 'GateSlot slot(request, GateDomain::Chat);' in lines[j]:
                for l in reversed(bypass_block + [
                    "            return CreateSimpleCompletion(resolved, on_queue_status, {});\n",
                    "        }\n",
                    "        ChatCompletionResult fail;\n",
                    "        fail.error = \"Binding resolution failed: no target available for \" + request.model.id;\n",
                    "        return fail;\n",
                    "    }\n",
                ]):
                    lines.insert(j, l)
                found = True
                break
        if found:
            break

# StreamToolAwareCompletion
found = False
for i, line in enumerate(lines):
    if 'ChatCompletionResult OpenAIClient::StreamToolAwareCompletion(const ChatRequestOptions& request,' in line:
        for j in range(i + 1, min(i + 20, len(lines))):
            if 'GateSlot slot(request, GateDomain::Chat);' in lines[j]:
                for l in reversed(bypass_block + [
                    "            return StreamToolAwareCompletion(resolved, tools, on_delta, on_queue_status, {});\n",
                    "        }\n",
                    "        ChatCompletionResult fail;\n",
                    "        fail.error = \"Binding resolution failed: no target available for \" + request.model.id;\n",
                    "        return fail;\n",
                    "    }\n",
                ]):
                    lines.insert(j, l)
                found = True
                break
        if found:
            break

# Rewrite CreateEmbedding
old_start = None
old_end = None
for i, line in enumerate(lines):
    if 'std::vector<std::vector<float>> OpenAIClient::CreateEmbedding(' in line:
        brace = 0
        for j in range(i, len(lines)):
            brace += lines[j].count('{')
            brace -= lines[j].count('}')
            if brace == 0:
                old_start = i
                old_end = j
                break
        break

if old_start is not None:
    new_body = [
        "std::vector<std::vector<float>> OpenAIClient::CreateEmbedding(const ProviderConfig& provider,\n",
        "                                                              const ModelConfig& model,\n",
        "                                                              const std::vector<std::string>& texts,\n",
        "                                                              const std::function<void(const ProviderQueueStatus&)>& on_queue_status) {\n",
        "    if (texts.empty()) return {};\n",
        "    // Binding bypass for embeddings\n",
        "    if (model.is_binding_model && model.bypass_queue) {\n",
        "        ChatRequestOptions synthetic;\n",
        "        synthetic.provider = provider;\n",
        "        synthetic.model = model;\n",
        "        ChatRequestOptions resolved = synthetic;\n",
        "        std::vector<ProviderConfig> all_providers;\n",
        "        {\n",
        "            std::lock_guard<std::mutex> lock(GateMapMutex());\n",
        "            for (const auto& kv : s_gate_provider_cache) all_providers.push_back(kv.second);\n",
        "        }\n",
        "        if (!ResolveBindingTarget(all_providers, model, synthetic, &resolved, on_queue_status)) {\n",
        "            throw std::runtime_error(\"Embedding binding resolution failed for \" + model.id);\n",
        "        }\n",
        "        return CreateEmbedding(resolved.provider, resolved.model, texts, on_queue_status);\n",
        "    }\n",
        "    ChatRequestOptions gate_request;\n",
        "    gate_request.provider = provider;\n",
        "    gate_request.model = model;\n",
        "    GateSlot slot(gate_request, GateDomain::Embedding);\n",
        "    if (!slot.Acquire(provider.name, on_queue_status)) {\n",
        "        throw std::runtime_error(\"Provider queue is full for embedding: \" + provider.name + \"/\" + model.id + \".\");\n",
        "    }\n",
        "    const std::string ptype = NormalizeProviderType(provider.provider_type);\n",
        "    if (ptype == \"ollama_local\") {\n",
        "        return RunOllamaLocalEmbedding(provider, model, texts, on_queue_status);\n",
        "    }\n",
        "    if (ptype == \"ollama_cloud\") {\n",
        "        return RunOllamaCloudEmbedding(provider, model, texts, on_queue_status);\n",
        "    }\n",
        "    return RunOpenAICompatibleEmbedding(provider, model, texts, on_queue_status);\n",
        "}\n",
    ]
    del lines[old_start:old_end + 1]
    for l in reversed(new_body):
        lines.insert(old_start, l)
    print('Rewrote CreateEmbedding')
else:
    print('WARN: old CreateEmbedding body not found')

# Add embedding stub implementations after GateSlot destructor
idx = None
for i, line in enumerate(lines):
    if 'GateSlot::~GateSlot() {' in line:
        j = i + 1
        while j < len(lines):
            if lines[j].strip().startswith('}'):
                idx = j + 1
                break
            j += 1
        break

stubs = [
    "\n",
    "std::vector<std::vector<float>> RunOllamaLocalEmbedding(const ProviderConfig& provider,\n",
    "                                                         const ModelConfig& model,\n",
    "                                                         const std::vector<std::string>& texts,\n",
    "                                                         const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/) {\n",
    "    (void)provider; (void)model; (void)texts;\n",
    "    throw std::runtime_error(\"RunOllamaLocalEmbedding not yet implemented.\");\n",
    "}\n",
    "\n",
    "std::vector<std::vector<float>> RunOllamaCloudEmbedding(const ProviderConfig& provider,\n",
    "                                                         const ModelConfig& model,\n",
    "                                                         const std::vector<std::string>& texts,\n",
    "                                                         const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/) {\n",
    "    (void)provider; (void)model; (void)texts;\n",
    "    throw std::runtime_error(\"RunOllamaCloudEmbedding not yet implemented.\");\n",
    "}\n",
    "\n",
    "std::vector<std::vector<float>> RunOpenAICompatibleEmbedding(const ProviderConfig& provider,\n",
    "                                                             const ModelConfig& model,\n",
    "                                                             const std::vector<std::string>& texts,\n",
    "                                                             const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/) {\n",
    "    (void)provider; (void)model; (void)texts;\n",
    "    throw std::runtime_error(\"RunOpenAICompatibleEmbedding not yet implemented.\");\n",
    "}\n",
]
if idx is not None:
    for l in reversed(stubs):
        lines.insert(idx, l)
    print('Inserted embedding stubs')
else:
    print('WARN: could not insert embedding stubs')

write_lines('src/openai_client.cpp', lines, enc)
print('Done openai_client.cpp step 2')

# ---------- openai_client.h ----------
lines, enc = read_lines('src/openai_client.h')
for i, line in enumerate(lines):
    if 'class OpenAIClient {' in line:
        decl = [
            "\n",
            "struct BindingTargetConfig;\n",
            "enum class BindingRoutingMode;\n",
            "\n",
            "bool ResolveBindingTarget(const std::vector<ProviderConfig>& providers,\n",
            "                          const ModelConfig& binding_model,\n",
            "                          const ChatRequestOptions& original_request,\n",
            "                          ChatRequestOptions* out_request,\n",
            "                          const std::function<void(const ProviderQueueStatus&)>& on_queue_status);\n",
            "\n",
            "std::vector<std::vector<float>> RunOllamaLocalEmbedding(const ProviderConfig& provider,\n",
            "                                                         const ModelConfig& model,\n",
            "                                                         const std::vector<std::string>& texts,\n",
            "                                                         const std::function<void(const ProviderQueueStatus&)>& on_queue_status);\n",
            "\n",
            "std::vector<std::vector<float>> RunOllamaCloudEmbedding(const ProviderConfig& provider,\n",
            "                                                         const ModelConfig& model,\n",
            "                                                         const std::vector<std::string>& texts,\n",
            "                                                         const std::function<void(const ProviderQueueStatus&)>& on_queue_status);\n",
            "\n",
            "std::vector<std::vector<float>> RunOpenAICompatibleEmbedding(const ProviderConfig& provider,\n",
            "                                                             const ModelConfig& model,\n",
            "                                                             const std::vector<std::string>& texts,\n",
            "                                                             const std::function<void(const ProviderQueueStatus&)>& on_queue_status);\n",
            "\n",
        ]
        for l in reversed(decl):
            lines.insert(i, l)
        break
write_lines('src/openai_client.h', lines, enc)
print('Done openai_client.h')

print('All edits complete.')
