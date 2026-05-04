import os, re

def read_file(path):
    with open(path, 'rb') as f:
        raw = f.read()
    if raw.startswith(b'\xff\xfe'):
        text = raw.decode('utf-16-le')
        eol = '\r\n' if '\r\n' in text else '\n'
        bom = True
    else:
        text = raw.decode('utf-8')
        eol = '\r\n' if '\r\n' in text else '\n'
        bom = False
    return text, eol, bom

def write_file(path, text, eol, bom):
    if bom:
        b = text.encode('utf-16-le')
    else:
        b = text.encode('utf-8')
    with open(path, 'wb') as f:
        f.write(b)

# ===================== openai_client.cpp =====================
text, eol, bom = read_file('src/openai_client.cpp')

# Ensure #include <unordered_map>
if '#include <unordered_map>' not in text:
    text = text.replace('#include <thread>', '#include <thread>' + eol + '#include <unordered_map>')

# Insert ResolveBindingTarget after GateSlot destructor
old = (
    "GateSlot::~GateSlot() {" + eol +
    "    if (acquired) {" + eol +
    "        ProviderRequestGate::Release(key);" + eol +
    "    }" + eol +
    "}" + eol + eol +
    "namespace {" + eol +
    "struct InternetHandleCloser {"
)
new = (
    "GateSlot::~GateSlot() {" + eol +
    "    if (acquired) {" + eol +
    "        ProviderRequestGate::Release(key);" + eol +
    "    }" + eol +
    "}" + eol + eol +
    "bool ResolveBindingTarget(const std::vector<ProviderConfig>& providers," + eol +
    "                          const ModelConfig& binding_model," + eol +
    "                          const ChatRequestOptions& original_request," + eol +
    "                          ChatRequestOptions* out_request," + eol +
    "                          const std::function<void(const ProviderQueueStatus&)>& on_queue_status) {" + eol +
    "    if (!binding_model.is_binding_model || binding_model.binding_targets.empty()) return false;" + eol +
    "    const bool round_robin = binding_model.binding_routing_mode == BindingRoutingMode::RoundRobin;" + eol +
    "    auto is_cooldown = [&](const BindingTargetConfig& t) -> bool {" + eol +
    "        return OpenAIClient::BindingCooldown(t.provider_id, t.model_id);" + eol +
    "    };" + eol +
    "    std::vector<const BindingTargetConfig*> candidates;" + eol +
    "    for (const auto& t : binding_model.binding_targets) {" + eol +
    "        if (!is_cooldown(t)) candidates.push_back(&t);" + eol +
    "    }" + eol +
    "    if (candidates.empty()) {" + eol +
    "        for (const auto& t : binding_model.binding_targets) {" + eol +
    "            candidates.push_back(&t);" + eol +
    "        }" + eol +
    "    }" + eol +
    "    const BindingTargetConfig* selected = nullptr;" + eol +
    "    if (round_robin) {" + eol +
    "        static std::unordered_map<std::string, size_t> rr_index;" + eol +
    "        static std::mutex rr_mtx;" + eol +
    "        std::lock_guard<std::mutex> lock(rr_mtx);" + eol +
    "        size_t& idx = rr_index[binding_model.id];" + eol +
    "        for (size_t attempt = 0; attempt < candidates.size(); ++attempt) {" + eol +
    "            const auto* candidate = candidates[idx % candidates.size()];" + eol +
    "            idx++;" + eol +
    "            int eff_active = 1, eff_queue = 100;" + eol +
    "            bool self_managed = false;" + eol +
    "            {" + eol +
    "                std::unique_lock<std::mutex> map_lock(GateMapMutex());" + eol +
    "                auto it = s_gate_provider_cache.find(candidate->provider_id);" + eol +
    "                if (it != s_gate_provider_cache.end()) {" + eol +
    "                    int p_active = it->second.max_active_requests;" + eol +
    "                    int p_queue = it->second.max_queue_size;" + eol +
    "                    int m_active = 0, m_queue = 0;" + eol +
    "                    for (const auto& m : it->second.models) {" + eol +
    "                        if (m.id == candidate->model_id) {" + eol +
    "                            m_active = m.max_active_requests;" + eol +
    "                            m_queue = m.max_queue_size;" + eol +
    "                            self_managed = m.self_managed_queue;" + eol +
    "                            break;" + eol +
    "                        }" + eol +
    "                    }" + eol +
    "                    eff_active = ComputeEffectiveMaxActive(p_active, m_active);" + eol +
    "                    eff_queue = ComputeEffectiveMaxQueue(p_queue, m_queue);" + eol +
    "                }" + eol +
    "            }" + eol +
    "            if (self_managed) { selected = candidate; break; }" + eol +
    "            if (ProviderRequestGate::Acquire({candidate->provider_id, candidate->model_id, GateDomain::Chat}, eff_active, eff_queue, on_queue_status)) {" + eol +
    "                selected = candidate;" + eol +
    "                break;" + eol +
    "            }" + eol +
    "        }" + eol +
    "    } else {" + eol +
    "        for (const auto* candidate : candidates) {" + eol +
    "            int eff_active = 1, eff_queue = 100;" + eol +
    "            bool self_managed = false;" + eol +
    "            {" + eol +
    "                std::unique_lock<std::mutex> map_lock(GateMapMutex());" + eol +
    "                auto it = s_gate_provider_cache.find(candidate->provider_id);" + eol +
    "                if (it != s_gate_provider_cache.end()) {" + eol +
    "                    int p_active = it->second.max_active_requests;" + eol +
    "                    int p_queue = it->second.max_queue_size;" + eol +
    "                    int m_active = 0, m_queue = 0;" + eol +
    "                    for (const auto& m : it->second.models) {" + eol +
    "                        if (m.id == candidate->model_id) {" + eol +
    "                            m_active = m.max_active_requests;" + eol +
    "                            m_queue = m.max_queue_size;" + eol +
    "                            self_managed = m.self_managed_queue;" + eol +
    "                            break;" + eol +
    "                        }" + eol +
    "                    }" + eol +
    "                    eff_active = ComputeEffectiveMaxActive(p_active, m_active);" + eol +
    "                    eff_queue = ComputeEffectiveMaxQueue(p_queue, m_queue);" + eol +
    "                }" + eol +
    "            }" + eol +
    "            if (self_managed) { selected = candidate; break; }" + eol +
    "            if (ProviderRequestGate::Acquire({candidate->provider_id, candidate->model_id, GateDomain::Chat}, eff_active, eff_queue, on_queue_status)) {" + eol +
    "                selected = candidate;" + eol +
    "                break;" + eol +
    "            }" + eol +
    "        }" + eol +
    "    }" + eol +
    "    if (!selected) return false;" + eol +
    "    for (const auto& p : providers) {" + eol +
    "        if (p.id != selected->provider_id) continue;" + eol +
    "        for (const auto& m : p.models) {" + eol +
    "            if (m.id == selected->model_id) {" + eol +
    "                out_request->provider = p;" + eol +
    "                out_request->model = m;" + eol +
    "                out_request->system_prompt = original_request.system_prompt;" + eol +
    "                out_request->temperature = original_request.temperature;" + eol +
    "                out_request->max_tokens = original_request.max_tokens;" + eol +
    "                out_request->messages = original_request.messages;" + eol +
    "                out_request->binding_model_id = binding_model.id;" + eol +
    "                out_request->binding_depth = original_request.binding_depth + 1;" + eol +
    "                return true;" + eol +
    "            }" + eol +
    "        }" + eol +
    "    }" + eol +
    "    return false;" + eol +
    "}" + eol + eol +
    "namespace {" + eol +
    "struct InternetHandleCloser {"
)

if old in text:
    text = text.replace(old, new)
    print('Inserted ResolveBindingTarget')
else:
    print('WARN: insertion point not found for ResolveBindingTarget')

# binding bypass blocks
bypass_intro = (
    "    // Binding bypass: resolve to concrete target and acquire its gate directly" + eol +
    "    if (request.model.is_binding_model && request.model.bypass_queue && request.binding_depth < 8) {" + eol +
    "        ChatRequestOptions resolved = request;" + eol +
    "        std::vector<ProviderConfig> all_providers;" + eol +
    "        {" + eol +
    "            std::lock_guard<std::mutex> lock(GateMapMutex());" + eol +
    "            for (const auto& kv : s_gate_provider_cache) all_providers.push_back(kv.second);" + eol +
    "        }" + eol +
    "        if (ResolveBindingTarget(all_providers, request.model, request, &resolved, on_queue_status)) {" + eol
)

# StreamChat
old_sc = (
    "ChatExecutionResult OpenAIClient::StreamChat(const ChatRequestOptions& request," + eol +
    "                                           const std::function<void(const std::string&)>& on_delta," + eol +
    "                                           const std::function<void(const ProviderQueueStatus&)>& on_queue_status," + eol +
    "                                           const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {" + eol +
    "    GateSlot slot(request, GateDomain::Chat);" + eol
)
new_sc = (
    "ChatExecutionResult OpenAIClient::StreamChat(const ChatRequestOptions& request," + eol +
    "                                           const std::function<void(const std::string&)>& on_delta," + eol +
    "                                           const std::function<void(const ProviderQueueStatus&)>& on_queue_status," + eol +
    "                                           const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {" + eol +
    bypass_intro +
    "            return StreamChat(resolved, on_delta, on_queue_status, {});" + eol +
    "        }" + eol +
    "        ChatExecutionResult fail;" + eol +
    "        fail.error = \"Binding resolution failed: no target available for \" + request.model.id;" + eol +
    "        return fail;" + eol +
    "    }" + eol +
    "    GateSlot slot(request, GateDomain::Chat);" + eol
)
if old_sc in text:
    text = text.replace(old_sc, new_sc)
else:
    print('WARN: StreamChat old not found')

# CreateToolAwareCompletion
old_cta = (
    "ChatCompletionResult OpenAIClient::CreateToolAwareCompletion(const ChatRequestOptions& request," + eol +
    "                                                           const std::vector<ChatToolDefinition>& tools," + eol +
    "                                                           const std::function<void(const ProviderQueueStatus&)>& on_queue_status," + eol +
    "                                                           const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {" + eol +
    "    GateSlot slot(request, GateDomain::Chat);" + eol
)
new_cta = (
    "ChatCompletionResult OpenAIClient::CreateToolAwareCompletion(const ChatRequestOptions& request," + eol +
    "                                                           const std::vector<ChatToolDefinition>& tools," + eol +
    "                                                           const std::function<void(const ProviderQueueStatus&)>& on_queue_status," + eol +
    "                                                           const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {" + eol +
    bypass_intro +
    "            return CreateToolAwareCompletion(resolved, tools, on_queue_status, {});" + eol +
    "        }" + eol +
    "        ChatCompletionResult fail;" + eol +
    "        fail.error = \"Binding resolution failed: no target available for \" + request.model.id;" + eol +
    "        return fail;" + eol +
    "    }" + eol +
    "    GateSlot slot(request, GateDomain::Chat);" + eol
)
if old_cta in text:
    text = text.replace(old_cta, new_cta)
else:
    print('WARN: CreateToolAwareCompletion old not found')

# CreateSimpleCompletion
old_csc = (
    "ChatCompletionResult OpenAIClient::CreateSimpleCompletion(const ChatRequestOptions& request," + eol +
    "                                                        const std::function<void(const ProviderQueueStatus&)>& on_queue_status," + eol +
    "                                                        const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {" + eol +
    "    GateSlot slot(request, GateDomain::Chat);" + eol
)
new_csc = (
    "ChatCompletionResult OpenAIClient::CreateSimpleCompletion(const ChatRequestOptions& request," + eol +
    "                                                        const std::function<void(const ProviderQueueStatus&)>& on_queue_status," + eol +
    "                                                        const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {" + eol +
    bypass_intro +
    "            return CreateSimpleCompletion(resolved, on_queue_status, {});" + eol +
    "        }" + eol +
    "        ChatCompletionResult fail;" + eol +
    "        fail.error = \"Binding resolution failed: no target available for \" + request.model.id;" + eol +
    "        return fail;" + eol +
    "    }" + eol +
    "    GateSlot slot(request, GateDomain::Chat);" + eol
)
if old_csc in text:
    text = text.replace(old_csc, new_csc)
else:
    print('WARN: CreateSimpleCompletion old not found')

# StreamToolAwareCompletion
old_stc = (
    "ChatCompletionResult OpenAIClient::StreamToolAwareCompletion(const ChatRequestOptions& request," + eol +
    "                                                             const std::vector<ChatToolDefinition>& tools," + eol +
    "                                                             const std::function<void(const std::string&)>& on_delta," + eol +
    "                                                             const std::function<void(const ProviderQueueStatus&)>& on_queue_status," + eol +
    "                                                             const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {" + eol +
    "    GateSlot slot(request, GateDomain::Chat);" + eol
)
new_stc = (
    "ChatCompletionResult OpenAIClient::StreamToolAwareCompletion(const ChatRequestOptions& request," + eol +
    "                                                             const std::vector<ChatToolDefinition>& tools," + eol +
    "                                                             const std::function<void(const std::string&)>& on_delta," + eol +
    "                                                             const std::function<void(const ProviderQueueStatus&)>& on_queue_status," + eol +
    "                                                             const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {" + eol +
    bypass_intro +
    "            return StreamToolAwareCompletion(resolved, tools, on_delta, on_queue_status, {});" + eol +
    "        }" + eol +
    "        ChatCompletionResult fail;" + eol +
    "        fail.error = \"Binding resolution failed: no target available for \" + request.model.id;" + eol +
    "        return fail;" + eol +
    "    }" + eol +
    "    GateSlot slot(request, GateDomain::Chat);" + eol
)
if old_stc in text:
    text = text.replace(old_stc, new_stc)
else:
    print('WARN: StreamToolAwareCompletion old not found')

# CreateEmbedding
old_embed = (
    "std::vector<std::vector<float>> OpenAIClient::CreateEmbedding(const ProviderConfig& provider," + eol +
    "                                                              const ModelConfig& model," + eol +
    "                                                              const std::vector<std::string>& texts," + eol +
    "                                                              const std::function<void(const ProviderQueueStatus&)>& on_queue_status) {" + eol +
    "    if (texts.empty()) return {};" + eol + eol +
    "    // Build a minimal ChatRequestOptions for gate acquisition (embedding domain)" + eol +
    "    ChatRequestOptions gate_request;" + eol +
    "    gate_request.provider = provider;" + eol +
    "    gate_request.model = model;" + eol + eol +
    "    GateSlot slot(gate_request, GateDomain::Embedding);" + eol +
    "    if (!slot.Acquire(provider.name, on_queue_status)) {" + eol +
    "        throw std::runtime_error(\"Provider queue is full for embedding: \" + provider.name + \"/\" + model.id + \".\");" + eol +
    "    }" + eol + eol +
    "    // Embedding implementations are currently provided by rag_service.cpp providers." + eol +
    "    // This stub avoids duplicating HTTP/JSON helpers; wire here when needed." + eol +
    "    (void)provider;" + eol +
    "    (void)model;" + eol +
    "    throw std::runtime_error(\"CreateEmbedding not yet implemented in OpenAIClient.\");" + eol +
    "}"
)
new_embed = (
    "std::vector<std::vector<float>> OpenAIClient::CreateEmbedding(const ProviderConfig& provider," + eol +
    "                                                              const ModelConfig& model," + eol +
    "                                                              const std::vector<std::string>& texts," + eol +
    "                                                              const std::function<void(const ProviderQueueStatus&)>& on_queue_status) {" + eol +
    "    if (texts.empty()) return {};" + eol +
    "    // Binding bypass for embeddings" + eol +
    "    if (model.is_binding_model && model.bypass_queue) {" + eol +
    "        ChatRequestOptions synthetic; synthetic.provider = provider; synthetic.model = model;" + eol +
    "        ChatRequestOptions resolved = synthetic;" + eol +
    "        std::vector<ProviderConfig> all_providers;" + eol +
    "        { std::lock_guard<std::mutex> lock(GateMapMutex()); for (const auto& kv : s_gate_provider_cache) all_providers.push_back(kv.second); }" + eol +
    "        if (!ResolveBindingTarget(all_providers, model, synthetic, &resolved, on_queue_status)) {" + eol +
    "            throw std::runtime_error(\"Embedding binding resolution failed for \" + model.id);" + eol +
    "        }" + eol +
    "        return CreateEmbedding(resolved.provider, resolved.model, texts, on_queue_status);" + eol +
    "    }" + eol +
    "    ChatRequestOptions gate_request;" + eol +
    "    gate_request.provider = provider;" + eol +
    "    gate_request.model = model;" + eol +
    "    GateSlot slot(gate_request, GateDomain::Embedding);" + eol +
    "    if (!slot.Acquire(provider.name, on_queue_status)) {" + eol +
    "        throw std::runtime_error(\"Provider queue is full for embedding: \" + provider.name + \"/\" + model.id + \".\");" + eol +
    "    }" + eol +
    "    const std::string ptype = NormalizeProviderType(provider.provider_type);" + eol +
    "    if (ptype == \"ollama_local\") {" + eol +
    "        return RunOllamaLocalEmbedding(provider, model, texts, on_queue_status);" + eol +
    "    }" + eol +
    "    if (ptype == \"ollama_cloud\") {" + eol +
    "        return RunOllamaCloudEmbedding(provider, model, texts, on_queue_status);" + eol +
    "    }" + eol +
    "    return RunOpenAICompatibleEmbedding(provider, model, texts, on_queue_status);" + eol +
    "}" + eol + eol +
    "std::vector<std::vector<float>> RunOllamaLocalEmbedding(const ProviderConfig& provider," + eol +
    "                                                         const ModelConfig& model," + eol +
    "                                                         const std::vector<std::string>& texts," + eol +
    "                                                         const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/) {" + eol +
    "    (void)provider; (void)model; (void)texts;" + eol +
    "    throw std::runtime_error(\"RunOllamaLocalEmbedding not yet implemented.\");" + eol +
    "}" + eol + eol +
    "std::vector<std::vector<float>> RunOllamaCloudEmbedding(const ProviderConfig& provider," + eol +
    "                                                         const ModelConfig& model," + eol +
    "                                                         const std::vector<std::string>& texts," + eol +
    "                                                         const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/) {" + eol +
    "    (void)provider; (void)model; (void)texts;" + eol +
    "    throw std::runtime_error(\"RunOllamaCloudEmbedding not yet implemented.\");" + eol +
    "}" + eol + eol +
    "std::vector<std::vector<float>> RunOpenAICompatibleEmbedding(const ProviderConfig& provider," + eol +
    "                                                             const ModelConfig& model," + eol +
    "                                                             const std::vector<std::string>& texts," + eol +
    "                                                             const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/) {" + eol +
    "    (void)provider; (void)model; (void)texts;" + eol +
    "    throw std::runtime_error(\"RunOpenAICompatibleEmbedding not yet implemented.\");" + eol +
    "}"
)

if old_embed in text:
    text = text.replace(old_embed, new_embed)
    print('Rewrote CreateEmbedding + added stubs')
else:
    print('WARN: old CreateEmbedding body not found')

write_file('src/openai_client.cpp', text, eol, bom)
print('Done openai_client.cpp')

# ===================== openai_client.h =====================
h_text, h_eol, h_bom = read_file('src/openai_client.h')

old_class = 'class OpenAIClient {'
new_class = (
    'struct BindingTargetConfig;' + h_eol +
    'enum class BindingRoutingMode;' + h_eol + h_eol +
    'bool ResolveBindingTarget(const std::vector<ProviderConfig>& providers,' + h_eol +
    '                          const ModelConfig& binding_model,' + h_eol +
    '                          const ChatRequestOptions& original_request,' + h_eol +
    '                          ChatRequestOptions* out_request,' + h_eol +
    '                          const std::function<void(const ProviderQueueStatus&)>& on_queue_status);' + h_eol + h_eol +
    'std::vector<std::vector<float>> RunOllamaLocalEmbedding(const ProviderConfig& provider,' + h_eol +
    '                                                         const ModelConfig& model,' + h_eol +
    '                                                         const std::vector<std::string>& texts,' + h_eol +
    '                                                         const std::function<void(const ProviderQueueStatus&)>& on_queue_status);' + h_eol + h_eol +
    'std::vector<std::vector<float>> RunOllamaCloudEmbedding(const ProviderConfig& provider,' + h_eol +
    '                                                         const ModelConfig& model,' + h_eol +
    '                                                         const std::vector<std::string>& texts,' + h_eol +
    '                                                         const std::function<void(const ProviderQueueStatus&)>& on_queue_status);' + h_eol + h_eol +
    'std::vector<std::vector<float>> RunOpenAICompatibleEmbedding(const ProviderConfig& provider,' + h_eol +
    '                                                             const ModelConfig& model,' + h_eol +
    '                                                             const std::vector<std::string>& texts,' + h_eol +
    '                                                             const std::function<void(const ProviderQueueStatus&)>& on_queue_status);' + h_eol + h_eol +
    'class OpenAIClient {'
)

if old_class in h_text:
    h_text = h_text.replace(old_class, new_class)
    print('Updated openai_client.h')
else:
    print('WARN: class OpenAIClient not found in header')

write_file('src/openai_client.h', h_text, h_eol, h_bom)
print('All edits complete.')
