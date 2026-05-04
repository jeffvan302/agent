import sys, os

def read_file(path):
    with open(path, 'rb') as f:
        raw = f.read()
    if raw.startswith(b'\xff\xfe'):
        text = raw.decode('utf-16-le')
        eol = '\r\n' if '\r\n' in text else '\n'
    elif raw.startswith(b'\xfe\xff'):
        text = raw.decode('utf-16-be')
        eol = '\r\n' if '\r\n' in text else '\n'
    else:
        text = raw.decode('utf-8')
        eol = '\r\n' if '\r\n' in text else '\n'
    lines = text.splitlines()
    return [l + eol for l in lines], eol

def write_file(path, lines, eol):
    text = ''.join(lines)
    with open(path, 'rb') as f:
        raw = f.read()
    if raw.startswith(b'\xff\xfe'):
        b = text.encode('utf-16-le')
        if not b.startswith(b'\xff\xfe'):
            b = b'\xff\xfe' + b
    else:
        b = text.encode('utf-8')
    with open(path, 'wb') as f:
        f.write(b)

# ===================== openai_client.cpp =====================
lines, eol = read_file('src/openai_client.cpp')

# 1. Add #include <unordered_map>
for i, line in enumerate(lines):
    if '#include <thread>' in line:
        if not any('#include <unordered_map>' in l for l in lines[max(0, i-2):i+3]):
            lines.insert(i + 1, '#include <unordered_map>' + eol)
        break

# 2. Insert ResolveBindingTarget after GateSlot destructor
idx = None
for i, line in enumerate(lines):
    if 'GateSlot::~GateSlot() {' in line:
        for j in range(i + 1, len(lines)):
            if lines[j].strip() == '}':
                k = j + 1
                while k < len(lines) and lines[k].strip() == '':
                    k += 1
                idx = k
                break
        break

if idx is not None:
    block = [
        eol,
        'bool ResolveBindingTarget(const std::vector<ProviderConfig>& providers,' + eol,
        '                          const ModelConfig& binding_model,' + eol,
        '                          const ChatRequestOptions& original_request,' + eol,
        '                          ChatRequestOptions* out_request,' + eol,
        '                          const std::function<void(const ProviderQueueStatus&)>& on_queue_status) {' + eol,
        '    if (!binding_model.is_binding_model || binding_model.binding_targets.empty()) return false;' + eol,
        '    const bool round_robin = binding_model.binding_routing_mode == BindingRoutingMode::RoundRobin;' + eol,
        '    auto is_cooldown = [&](const BindingTargetConfig& t) -> bool {' + eol,
        '        return OpenAIClient::BindingCooldown(t.provider_id, t.model_id);' + eol,
        '    };' + eol,
        '    std::vector<const BindingTargetConfig*> candidates;' + eol,
        '    for (const auto& t : binding_model.binding_targets) {' + eol,
        '        if (!is_cooldown(t)) candidates.push_back(&t);' + eol,
        '    }' + eol,
        '    if (candidates.empty()) {' + eol,
        '        for (const auto& t : binding_model.binding_targets) {' + eol,
        '            candidates.push_back(&t);' + eol,
        '        }' + eol,
        '    }' + eol,
        '    const BindingTargetConfig* selected = nullptr;' + eol,
        '    if (round_robin) {' + eol,
        '        static std::unordered_map<std::string, size_t> rr_index;' + eol,
        '        static std::mutex rr_mtx;' + eol,
        '        std::lock_guard<std::mutex> lock(rr_mtx);' + eol,
        '        size_t& idx = rr_index[binding_model.id];' + eol,
        '        for (size_t attempt = 0; attempt < candidates.size(); ++attempt) {' + eol,
        '            const auto* candidate = candidates[idx % candidates.size()];' + eol,
        '            idx++;' + eol,
        '            int eff_active = 1, eff_queue = 100;' + eol,
        '            bool self_managed = false;' + eol,
        '            {' + eol,
        '                std::unique_lock<std::mutex> map_lock(GateMapMutex());' + eol,
        '                auto it = s_gate_provider_cache.find(candidate->provider_id);' + eol,
        '                if (it != s_gate_provider_cache.end()) {' + eol,
        '                    int p_active = it->second.max_active_requests;' + eol,
        '                    int p_queue = it->second.max_queue_size;' + eol,
        '                    int m_active = 0, m_queue = 0;' + eol,
        '                    for (const auto& m : it->second.models) {' + eol,
        '                        if (m.id == candidate->model_id) {' + eol,
        '                            m_active = m.max_active_requests;' + eol,
        '                            m_queue = m.max_queue_size;' + eol,
        '                            self_managed = m.self_managed_queue;' + eol,
        '                            break;' + eol,
        '                        }' + eol,
        '                    }' + eol,
        '                    eff_active = ComputeEffectiveMaxActive(p_active, m_active);' + eol,
        '                    eff_queue = ComputeEffectiveMaxQueue(p_queue, m_queue);' + eol,
        '                }' + eol,
        '            }' + eol,
        '            if (self_managed) { selected = candidate; break; }' + eol,
        '            if (ProviderRequestGate::Acquire({candidate->provider_id, candidate->model_id, GateDomain::Chat}, eff_active, eff_queue, on_queue_status)) {' + eol,
        '                selected = candidate;' + eol,
        '                break;' + eol,
        '            }' + eol,
        '        }' + eol,
        '    } else {' + eol,
        '        for (const auto* candidate : candidates) {' + eol,
        '            int eff_active = 1, eff_queue = 100;' + eol,
        '            bool self_managed = false;' + eol,
        '            {' + eol,
        '                std::unique_lock<std::mutex> map_lock(GateMapMutex());' + eol,
        '                auto it = s_gate_provider_cache.find(candidate->provider_id);' + eol,
        '                if (it != s_gate_provider_cache.end()) {' + eol,
        '                    int p_active = it->second.max_active_requests;' + eol,
        '                    int p_queue = it->second.max_queue_size;' + eol,
        '                    int m_active = 0, m_queue = 0;' + eol,
        '                    for (const auto& m : it->second.models) {' + eol,
        '                        if (m.id == candidate->model_id) {' + eol,
        '                            m_active = m.max_active_requests;' + eol,
        '                            m_queue = m.max_queue_size;' + eol,
        '                            self_managed = m.self_managed_queue;' + eol,
        '                            break;' + eol,
        '                        }' + eol,
        '                    }' + eol,
        '                    eff_active = ComputeEffectiveMaxActive(p_active, m_active);' + eol,
        '                    eff_queue = ComputeEffectiveMaxQueue(p_queue, m_queue);' + eol,
        '                }' + eol,
        '            }' + eol,
        '            if (self_managed) { selected = candidate; break; }' + eol,
        '            if (ProviderRequestGate::Acquire({candidate->provider_id, candidate->model_id, GateDomain::Chat}, eff_active, eff_queue, on_queue_status)) {' + eol,
        '                selected = candidate;' + eol,
        '                break;' + eol,
        '            }' + eol,
        '        }' + eol,
        '    }' + eol,
        '    if (!selected) return false;' + eol,
        '    for (const auto& p : providers) {' + eol,
        '        if (p.id != selected->provider_id) continue;' + eol,
        '        for (const auto& m : p.models) {' + eol,
        '            if (m.id == selected->model_id) {' + eol,
        '                out_request->provider = p;' + eol,
        '                out_request->model = m;' + eol,
        '                out_request->system_prompt = original_request.system_prompt;' + eol,
        '                out_request->temperature = original_request.temperature;' + eol,
        '                out_request->max_tokens = original_request.max_tokens;' + eol,
        '                out_request->messages = original_request.messages;' + eol,
        '                out_request->binding_model_id = binding_model.id;' + eol,
        '                out_request->binding_depth = original_request.binding_depth + 1;' + eol,
        '                return true;' + eol,
        '            }' + eol,
        '        }' + eol,
        '    }' + eol,
        '    return false;' + eol,
        '}' + eol,
        eol,
    ]
    for l in reversed(block):
        lines.insert(idx, l)
    print('Inserted ResolveBindingTarget')
else:
    print('WARN: insertion point not found for ResolveBindingTarget')

# 3. Insert binding bypass blocks into chat functions
bypass = [
    '    // Binding bypass: resolve to concrete target and acquire its gate directly' + eol,
    '    if (request.model.is_binding_model && request.model.bypass_queue && request.binding_depth < 8) {' + eol,
    '        ChatRequestOptions resolved = request;' + eol,
    '        std::vector<ProviderConfig> all_providers;' + eol,
    '        {' + eol,
    '            std::lock_guard<std::mutex> lock(GateMapMutex());' + eol,
    '            for (const auto& kv : s_gate_provider_cache) all_providers.push_back(kv.second);' + eol,
    '        }' + eol,
    '        if (ResolveBindingTarget(all_providers, request.model, request, &resolved, on_queue_status)) {' + eol,
]

# StreamChat
for i, line in enumerate(lines):
    if 'ChatExecutionResult OpenAIClient::StreamChat(const ChatRequestOptions& request,' in line:
        for j in range(i + 1, min(i + 15, len(lines))):
            if 'GateSlot slot(request, GateDomain::Chat);' in lines[j]:
                for l in reversed([
                    '            return StreamChat(resolved, on_delta, on_queue_status, {});' + eol,
                    '        }' + eol,
                    '        ChatExecutionResult fail;' + eol,
                    '        fail.error = "Binding resolution failed: no target available for " + request.model.id;' + eol,
                    '        return fail;' + eol,
                    '    }' + eol,
                ]):
                    lines.insert(j, l)
                for l in reversed(bypass):
                    lines.insert(j, l)
                print('Patched StreamChat')
                break
        break

# CreateToolAwareCompletion
for i, line in enumerate(lines):
    if 'ChatCompletionResult OpenAIClient::CreateToolAwareCompletion(const ChatRequestOptions& request,' in line:
        for j in range(i + 1, min(i + 15, len(lines))):
            if 'GateSlot slot(request, GateDomain::Chat);' in lines[j]:
                for l in reversed([
                    '            return CreateToolAwareCompletion(resolved, tools, on_queue_status, {});' + eol,
                    '        }' + eol,
                    '        ChatCompletionResult fail;' + eol,
                    '        fail.error = "Binding resolution failed: no target available for " + request.model.id;' + eol,
                    '        return fail;' + eol,
                    '    }' + eol,
                ]):
                    lines.insert(j, l)
                for l in reversed(bypass):
                    lines.insert(j, l)
                print('Patched CreateToolAwareCompletion')
                break
        break

# CreateSimpleCompletion
for i, line in enumerate(lines):
    if 'ChatCompletionResult OpenAIClient::CreateSimpleCompletion(const ChatRequestOptions& request,' in line:
        for j in range(i + 1, min(i + 15, len(lines))):
            if 'GateSlot slot(request, GateDomain::Chat);' in lines[j]:
                for l in reversed([
                    '            return CreateSimpleCompletion(resolved, on_queue_status, {});' + eol,
                    '        }' + eol,
                    '        ChatCompletionResult fail;' + eol,
                    '        fail.error = "Binding resolution failed: no target available for " + request.model.id;' + eol,
                    '        return fail;' + eol,
                    '    }' + eol,
                ]):
                    lines.insert(j, l)
                for l in reversed(bypass):
                    lines.insert(j, l)
                print('Patched CreateSimpleCompletion')
                break
        break

# StreamToolAwareCompletion
for i, line in enumerate(lines):
    if 'ChatCompletionResult OpenAIClient::StreamToolAwareCompletion(const ChatRequestOptions& request,' in line:
        for j in range(i + 1, min(i + 15, len(lines))):
            if 'GateSlot slot(request, GateDomain::Chat);' in lines[j]:
                for l in reversed([
                    '            return StreamToolAwareCompletion(resolved, tools, on_delta, on_queue_status, {});' + eol,
                    '        }' + eol,
                    '        ChatCompletionResult fail;' + eol,
                    '        fail.error = "Binding resolution failed: no target available for " + request.model.id;' + eol,
                    '        return fail;' + eol,
                    '    }' + eol,
                ]):
                    lines.insert(j, l)
                for l in reversed(bypass):
                    lines.insert(j, l)
                print('Patched StreamToolAwareCompletion')
                break
        break

# 4. Rewrite CreateEmbedding
start = None
end = None
for i, line in enumerate(lines):
    if 'std::vector<std::vector<float>> OpenAIClient::CreateEmbedding(' in line:
        brace = 0
        for j in range(i, len(lines)):
            brace += lines[j].count('{')
            brace -= lines[j].count('}')
            if brace == 0:
                start = i
                end = j
                break
        break

if start is not None:
    new_body = [
        'std::vector<std::vector<float>> OpenAIClient::CreateEmbedding(const ProviderConfig& provider,' + eol,
        '                                                              const ModelConfig& model,' + eol,
        '                                                              const std::vector<std::string>& texts,' + eol,
        '                                                              const std::function<void(const ProviderQueueStatus&)>& on_queue_status) {' + eol,
        '    if (texts.empty()) return {};' + eol,
        '    if (model.is_binding_model && model.bypass_queue) {' + eol,
        '        ChatRequestOptions synthetic; synthetic.provider = provider; synthetic.model = model;' + eol,
        '        ChatRequestOptions resolved = synthetic;' + eol,
        '        std::vector<ProviderConfig> all_providers;' + eol,
        '        { std::lock_guard<std::mutex> lock(GateMapMutex()); for (const auto& kv : s_gate_provider_cache) all_providers.push_back(kv.second); }' + eol,
        '        if (!ResolveBindingTarget(all_providers, model, synthetic, &resolved, on_queue_status)) {' + eol,
        '            throw std::runtime_error("Embedding binding resolution failed for " + model.id);' + eol,
        '        }' + eol,
        '        return CreateEmbedding(resolved.provider, resolved.model, texts, on_queue_status);' + eol,
        '    }' + eol,
        '    ChatRequestOptions gate_request;' + eol,
        '    gate_request.provider = provider;' + eol,
        '    gate_request.model = model;' + eol,
        '    GateSlot slot(gate_request, GateDomain::Embedding);' + eol,
        '    if (!slot.Acquire(provider.name, on_queue_status)) {' + eol,
        '        throw std::runtime_error("Provider queue is full for embedding: " + provider.name + "/" + model.id + ".");' + eol,
        '    }' + eol,
        '    const std::string ptype = NormalizeProviderType(provider.provider_type);' + eol,
        '    if (ptype == "ollama_local") {' + eol,
        '        return RunOllamaLocalEmbedding(provider, model, texts, on_queue_status);' + eol,
        '    }' + eol,
        '    if (ptype == "ollama_cloud") {' + eol,
        '        return RunOllamaCloudEmbedding(provider, model, texts, on_queue_status);' + eol,
        '    }' + eol,
        '    return RunOpenAICompatibleEmbedding(provider, model, texts, on_queue_status);' + eol,
        '}' + eol,
        eol,
        'std::vector<std::vector<float>> RunOllamaLocalEmbedding(const ProviderConfig& provider,' + eol,
        '                                                         const ModelConfig& model,' + eol,
        '                                                         const std::vector<std::string>& texts,' + eol,
        '                                                         const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/) {' + eol,
        '    (void)provider; (void)model; (void)texts;' + eol,
        '    throw std::runtime_error("RunOllamaLocalEmbedding not yet implemented.");' + eol,
        '}' + eol,
        eol,
        'std::vector<std::vector<float>> RunOllamaCloudEmbedding(const ProviderConfig& provider,' + eol,
        '                                                         const ModelConfig& model,' + eol,
        '                                                         const std::vector<std::string>& texts,' + eol,
        '                                                         const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/) {' + eol,
        '    (void)provider; (void)model; (void)texts;' + eol,
        '    throw std::runtime_error("RunOllamaCloudEmbedding not yet implemented.");' + eol,
        '}' + eol,
        eol,
        'std::vector<std::vector<float>> RunOpenAICompatibleEmbedding(const ProviderConfig& provider,' + eol,
        '                                                             const ModelConfig& model,' + eol,
        '                                                             const std::vector<std::string>& texts,' + eol,
        '                                                             const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/) {' + eol,
        '    (void)provider; (void)model; (void)texts;' + eol,
        '    throw std::runtime_error("RunOpenAICompatibleEmbedding not yet implemented.");' + eol,
        '}' + eol,
    ]
    del lines[start:end + 1]
    for l in reversed(new_body):
        lines.insert(start, l)
    print('Rewrote CreateEmbedding + added stubs')
else:
    print('WARN: old CreateEmbedding body not found')

write_file('src/openai_client.cpp', lines, eol)
print('Done openai_client.cpp')

# ===================== openai_client.h =====================
lines, eol = read_file('src/openai_client.h')
for i, line in enumerate(lines):
    if 'class OpenAIClient {' in line:
        decl = [
            eol,
            'struct BindingTargetConfig;' + eol,
            'enum class BindingRoutingMode;' + eol,
            eol,
            'bool ResolveBindingTarget(const std::vector<ProviderConfig>& providers,' + eol,
            '                          const ModelConfig& binding_model,' + eol,
            '                          const ChatRequestOptions& original_request,' + eol,
            '                          ChatRequestOptions* out_request,' + eol,
            '                          const std::function<void(const ProviderQueueStatus&)>& on_queue_status);' + eol,
            eol,
            'std::vector<std::vector<float>> RunOllamaLocalEmbedding(const ProviderConfig& provider,' + eol,
            '                                                         const ModelConfig& model,' + eol,
            '                                                         const std::vector<std::string>& texts,' + eol,
            '                                                         const std::function<void(const ProviderQueueStatus&)>& on_queue_status);' + eol,
            eol,
            'std::vector<std::vector<float>> RunOllamaCloudEmbedding(const ProviderConfig& provider,' + eol,
            '                                                         const ModelConfig& model,' + eol,
            '                                                         const std::vector<std::string>& texts,' + eol,
            '                                                         const std::function<void(const ProviderQueueStatus&)>& on_queue_status);' + eol,
            eol,
            'std::vector<std::vector<float>> RunOpenAICompatibleEmbedding(const ProviderConfig& provider,' + eol,
            '                                                             const ModelConfig& model,' + eol,
            '                                                             const std::vector<std::string>& texts,' + eol,
            '                                                             const std::function<void(const ProviderQueueStatus&)>& on_queue_status);' + eol,
            eol,
        ]
        for l in reversed(decl):
            lines.insert(i, l)
        break
write_file('src/openai_client.h', lines, eol)
print('Done openai_client.h')

print('All edits complete.')
