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
    return text.splitlines(), eol, raw[:2]

def write_file(path, lines, eol, bom):
    text = eol.join(lines)
    b = text.encode('utf-16-le')
    if bom == b'':
        b = b.encode('utf-8')
    else:
        if not b.startswith(bom):
            b = bom + b
    with open(path, 'wb') as f:
        f.write(b)

def find_matching_brace(lines, start):
    depth = 0
    for j in range(start, len(lines)):
        for ch in lines[j]:
            if ch == '{':
                depth += 1
            elif ch == '}':
                depth -= 1
                if depth == 0:
                    return j
    return None

# ===================== openai_client.cpp =====================
lines, eol, bom = read_file('src/openai_client.cpp')

# 1. Add #include <unordered_map>
for i, line in enumerate(lines):
    if '#include <thread>' in line:
        if not any('#include <unordered_map>' in l for l in lines[max(0, i-2):i+3]):
            lines.insert(i + 1, '#include <unordered_map>')
        break

# 2. Insert ResolveBindingTarget after GateSlot destructor
for i, line in enumerate(lines):
    if 'GateSlot::~GateSlot() {' in line:
        j = find_matching_brace(lines, i)
        if j is None:
            print('WARN: could not find matching brace for GateSlot destructor')
            sys.exit(1)
        idx = j + 1
        while idx < len(lines) and lines[idx].strip() == '':
            idx += 1
        block = [
            '',
            'bool ResolveBindingTarget(const std::vector<ProviderConfig>& providers,',
            '                          const ModelConfig& binding_model,',
            '                          const ChatRequestOptions& original_request,',
            '                          ChatRequestOptions* out_request,',
            '                          const std::function<void(const ProviderQueueStatus&)>& on_queue_status) {',
            '    if (!binding_model.is_binding_model || binding_model.binding_targets.empty()) return false;',
            '    const bool round_robin = binding_model.binding_routing_mode == BindingRoutingMode::RoundRobin;',
            '    auto is_cooldown = [&](const BindingTargetConfig& t) -> bool {',
            '        return OpenAIClient::BindingCooldown(t.provider_id, t.model_id);',
            '    };',
            '    std::vector<const BindingTargetConfig*> candidates;',
            '    for (const auto& t : binding_model.binding_targets) {',
            '        if (!is_cooldown(t)) candidates.push_back(&t);',
            '    }',
            '    if (candidates.empty()) {',
            '        for (const auto& t : binding_model.binding_targets) {',
            '            candidates.push_back(&t);',
            '        }',
            '    }',
            '    const BindingTargetConfig* selected = nullptr;',
            '    if (round_robin) {',
            '        static std::unordered_map<std::string, size_t> rr_index;',
            '        static std::mutex rr_mtx;',
            '        std::lock_guard<std::mutex> lock(rr_mtx);',
            '        size_t& idx = rr_index[binding_model.id];',
            '        for (size_t attempt = 0; attempt < candidates.size(); ++attempt) {',
            '            const auto* candidate = candidates[idx % candidates.size()];',
            '            idx++;',
            '            int eff_active = 1, eff_queue = 100;',
            '            bool self_managed = false;',
            '            {',
            '                std::unique_lock<std::mutex> map_lock(GateMapMutex());',
            '                auto it = s_gate_provider_cache.find(candidate->provider_id);',
            '                if (it != s_gate_provider_cache.end()) {',
            '                    int p_active = it->second.max_active_requests;',
            '                    int p_queue = it->second.max_queue_size;',
            '                    int m_active = 0, m_queue = 0;',
            '                    for (const auto& m : it->second.models) {',
            '                        if (m.id == candidate->model_id) {',
            '                            m_active = m.max_active_requests;',
            '                            m_queue = m.max_queue_size;',
            '                            self_managed = m.self_managed_queue;',
            '                            break;',
            '                        }',
            '                    }',
            '                    eff_active = ComputeEffectiveMaxActive(p_active, m_active);',
            '                    eff_queue = ComputeEffectiveMaxQueue(p_queue, m_queue);',
            '                }',
            '            }',
            '            if (self_managed) { selected = candidate; break; }',
            '            if (ProviderRequestGate::Acquire({candidate->provider_id, candidate->model_id, GateDomain::Chat}, eff_active, eff_queue, on_queue_status)) {',
            '                selected = candidate;',
            '                break;',
            '            }',
            '        }',
            '    } else {',
            '        for (const auto* candidate : candidates) {',
            '            int eff_active = 1, eff_queue = 100;',
            '            bool self_managed = false;',
            '            {',
            '                std::unique_lock<std::mutex> map_lock(GateMapMutex());',
            '                auto it = s_gate_provider_cache.find(candidate->provider_id);',
            '                if (it != s_gate_provider_cache.end()) {',
            '                    int p_active = it->second.max_active_requests;',
            '                    int p_queue = it->second.max_queue_size;',
            '                    int m_active = 0, m_queue = 0;',
            '                    for (const auto& m : it->second.models) {',
            '                        if (m.id == candidate->model_id) {',
            '                            m_active = m.max_active_requests;',
            '                            m_queue = m.max_queue_size;',
            '                            self_managed = m.self_managed_queue;',
            '                            break;',
            '                        }',
            '                    }',
            '                    eff_active = ComputeEffectiveMaxActive(p_active, m_active);',
            '                    eff_queue = ComputeEffectiveMaxQueue(p_queue, m_queue);',
            '                }',
            '            }',
            '            if (self_managed) { selected = candidate; break; }',
            '            if (ProviderRequestGate::Acquire({candidate->provider_id, candidate->model_id, GateDomain::Chat}, eff_active, eff_queue, on_queue_status)) {',
            '                selected = candidate;',
            '                break;',
            '            }',
            '        }',
            '    }',
            '    if (!selected) return false;',
            '    for (const auto& p : providers) {',
            '        if (p.id != selected->provider_id) continue;',
            '        for (const auto& m : p.models) {',
            '            if (m.id == selected->model_id) {',
            '                out_request->provider = p;',
            '                out_request->model = m;',
            '                out_request->system_prompt = original_request.system_prompt;',
            '                out_request->temperature = original_request.temperature;',
            '                out_request->max_tokens = original_request.max_tokens;',
            '                out_request->messages = original_request.messages;',
            '                out_request->binding_model_id = binding_model.id;',
            '                out_request->binding_depth = original_request.binding_depth + 1;',
            '                return true;',
            '            }',
            '        }',
            '    }',
            '    return false;',
            '}',',
            '',
        ]
        for l in reversed(block):
            lines.insert(idx, l)
        print('Inserted ResolveBindingTarget')
        break

# 3. Insert binding bypass blocks into chat functions
bypass = [
    '    // Binding bypass: resolve to concrete target and acquire its gate directly',
    '    if (request.model.is_binding_model && request.model.bypass_queue && request.binding_depth < 8) {',
    '        ChatRequestOptions resolved = request;',
    '        std::vector<ProviderConfig> all_providers;',
    '        {',
    '            std::lock_guard<std::mutex> lock(GateMapMutex());',
    '            for (const auto& kv : s_gate_provider_cache) all_providers.push_back(kv.second);',
    '        }',
    '        if (ResolveBindingTarget(all_providers, request.model, request, &resolved, on_queue_status)) {',
]

def patch_after_slot(lines, func_sig, extra_lines):
    for i, line in enumerate(lines):
        if func_sig in line:
            for j in range(i + 1, min(i + 15, len(lines))):
                if 'GateSlot slot(request, GateDomain::Chat);' in lines[j]:
                    for l in reversed(extra_lines):
                        lines.insert(j, l)
                    for l in reversed(bypass):
                        lines.insert(j, l)
                    print('Patched ' + func_sig)
                    return True
    print('WARN: could not patch ' + func_sig)
    return False

patch_after_slot(lines, 'ChatExecutionResult OpenAIClient::StreamChat(const ChatRequestOptions& request,', [
    '            return StreamChat(resolved, on_delta, on_queue_status, {});',
    '        }',
    '        ChatExecutionResult fail;',
    '        fail.error = "Binding resolution failed: no target available for " + request.model.id;',
    '        return fail;',
    '    }',
])

patch_after_slot(lines, 'ChatCompletionResult OpenAIClient::CreateToolAwareCompletion(const ChatRequestOptions& request,', [
    '            return CreateToolAwareCompletion(resolved, tools, on_queue_status, {});',
    '        }',
    '        ChatCompletionResult fail;',
    '        fail.error = "Binding resolution failed: no target available for " + request.model.id;',
    '        return fail;',
    '    }',
])

patch_after_slot(lines, 'ChatCompletionResult OpenAIClient::CreateSimpleCompletion(const ChatRequestOptions& request,', [
    '            return CreateSimpleCompletion(resolved, on_queue_status, {});',
    '        }',
    '        ChatCompletionResult fail;',
    '        fail.error = "Binding resolution failed: no target available for " + request.model.id;',
    '        return fail;',
    '    }',
])

patch_after_slot(lines, 'ChatCompletionResult OpenAIClient::StreamToolAwareCompletion(const ChatRequestOptions& request,', [
    '            return StreamToolAwareCompletion(resolved, tools, on_delta, on_queue_status, {});',
    '        }',
    '        ChatCompletionResult fail;',
    '        fail.error = "Binding resolution failed: no target available for " + request.model.id;',
    '        return fail;',
    '    }',
])

# 4. Rewrite CreateEmbedding
for i, line in enumerate(lines):
    if 'std::vector<std::vector<float>> OpenAIClient::CreateEmbedding(' in line:
        j = find_matching_brace(lines, i)
        if j is None:
            print('WARN: could not find brace for CreateEmbedding')
            sys.exit(1)
        new_body = [
            'std::vector<std::vector<float>> OpenAIClient::CreateEmbedding(const ProviderConfig& provider,',
            '                                                              const ModelConfig& model,',
            '                                                              const std::vector<std::string>& texts,',
            '                                                              const std::function<void(const ProviderQueueStatus&)>& on_queue_status) {',
            '    if (texts.empty()) return {};',
            '    if (model.is_binding_model && model.bypass_queue) {',
            '        ChatRequestOptions synthetic; synthetic.provider = provider; synthetic.model = model;',
            '        ChatRequestOptions resolved = synthetic;',
            '        std::vector<ProviderConfig> all_providers;',
            '        { std::lock_guard<std::mutex> lock(GateMapMutex()); for (const auto& kv : s_gate_provider_cache) all_providers.push_back(kv.second); }',
            '        if (!ResolveBindingTarget(all_providers, model, synthetic, &resolved, on_queue_status)) {',
            '            throw std::runtime_error("Embedding binding resolution failed for " + model.id);',
            '        }',
            '        return CreateEmbedding(resolved.provider, resolved.model, texts, on_queue_status);',
            '    }',
            '    ChatRequestOptions gate_request;',
            '    gate_request.provider = provider;',
            '    gate_request.model = model;',
            '    GateSlot slot(gate_request, GateDomain::Embedding);',
            '    if (!slot.Acquire(provider.name, on_queue_status)) {',
            '        throw std::runtime_error("Provider queue is full for embedding: " + provider.name + "/" + model.id + ".");',
            '    }',
            '    const std::string ptype = NormalizeProviderType(provider.provider_type);',
            '    if (ptype == "ollama_local") {',
            '        return RunOllamaLocalEmbedding(provider, model, texts, on_queue_status);',
            '    }',
            '    if (ptype == "ollama_cloud") {',
            '        return RunOllamaCloudEmbedding(provider, model, texts, on_queue_status);',
            '    }',
            '    return RunOpenAICompatibleEmbedding(provider, model, texts, on_queue_status);',
            '}',
            '',
            'std::vector<std::vector<float>> RunOllamaLocalEmbedding(const ProviderConfig& provider,',
            '                                                         const ModelConfig& model,',
            '                                                         const std::vector<std::string>& texts,',
            '                                                         const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/) {',
            '    (void)provider; (void)model; (void)texts;',
            '    throw std::runtime_error("RunOllamaLocalEmbedding not yet implemented.");',
            '}',
            '',
            'std::vector<std::vector<float>> RunOllamaCloudEmbedding(const ProviderConfig& provider,',
            '                                                         const ModelConfig& model,',
            '                                                         const std::vector<std::string>& texts,',
            '                                                         const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/) {',
            '    (void)provider; (void)model; (void)texts;',
            '    throw std::runtime_error("RunOllamaCloudEmbedding not yet implemented.");',
            '}',
            '',
            'std::vector<std::vector<float>> RunOpenAICompatibleEmbedding(const ProviderConfig& provider,',
            '                                                             const ModelConfig& model,',
            '                                                             const std::vector<std::string>& texts,',
            '                                                             const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/) {',
            '    (void)provider; (void)model; (void)texts;',
            '    throw std::runtime_error("RunOpenAICompatibleEmbedding not yet implemented.");',
            '}',
        ]
        lines[i:j+1] = new_body
        print('Rewrote CreateEmbedding + added stubs')
        break

write_file('src/openai_client.cpp', lines, eol, bom)
print('Done openai_client.cpp')

# ===================== openai_client.h =====================
lines, eol, bom = read_file('src/openai_client.h')
for i, line in enumerate(lines):
    if 'class OpenAIClient {' in line:
        decl = [
            '',
            'struct BindingTargetConfig;',
            'enum class BindingRoutingMode;',
            '',
            'bool ResolveBindingTarget(const std::vector<ProviderConfig>& providers,',
            '                          const ModelConfig& binding_model,',
            '                          const ChatRequestOptions& original_request,',
            '                          ChatRequestOptions* out_request,',
            '                          const std::function<void(const ProviderQueueStatus&)>& on_queue_status);',
            '',
            'std::vector<std::vector<float>> RunOllamaLocalEmbedding(const ProviderConfig& provider,',
            '                                                         const ModelConfig& model,',
            '                                                         const std::vector<std::string>& texts,',
            '                                                         const std::function<void(const ProviderQueueStatus&)>& on_queue_status);',
            '',
            'std::vector<std::vector<float>> RunOllamaCloudEmbedding(const ProviderConfig& provider,',
            '                                                         const ModelConfig& model,',
            '                                                         const std::vector<std::string>& texts,',
            '                                                         const std::function<void(const ProviderQueueStatus&)>& on_queue_status);',
            '',
            'std::vector<std::vector<float>> RunOpenAICompatibleEmbedding(const ProviderConfig& provider,',
            '                                                             const ModelConfig& model,',
            '                                                             const std::vector<std::string>& texts,',
            '                                                             const std::function<void(const ProviderQueueStatus&)>& on_queue_status);',
            '',
        ]
        for l in reversed(decl):
            lines.insert(i, l)
        break
write_file('src/openai_client.h', lines, eol, bom)
print('Done openai_client.h')

print('All edits complete.')
