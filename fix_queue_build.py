import re

def read_utf8(path):
    with open(path, 'rb') as f:
        raw = f.read()
    for enc in ['utf-8-sig', 'utf-8', 'utf-16-le', 'utf-16-be']:
        try:
            return raw.decode(enc), enc
        except Exception:
            continue
    raise RuntimeError('Cannot decode ' + path)

def write_utf8(path, text, enc):
    with open(path, 'wb') as f:
        f.write(text.encode(enc))

# ============================================================
# Fix 1: provider_manager.cpp – add bypass_check_ member in ModelEditorDialog
# ============================================================
# The member declarations block in ModelEditorDialog is at line ~2570
# We need to add HWND bypass_check_ = nullptr; next to self_managed_check_
text, enc = read_utf8('src/provider_manager.cpp')

# Fix the bypass member inside ModelEditorDialog
old_members = (
    "    HWND max_active_label_ = nullptr;\r\n"
    "    HWND max_active_edit_ = nullptr;\r\n"
    "    HWND max_queue_label_ = nullptr;\r\n"
    "    HWND max_queue_edit_ = nullptr;\r\n"
    "    HWND self_managed_check_ = nullptr;\r\n};\r\n"
    "\r\n"
    "struct ProviderParsedUrl {"
)
new_members = (
    "    HWND max_active_label_ = nullptr;\r\n"
    "    HWND max_active_edit_ = nullptr;\r\n"
    "    HWND max_queue_label_ = nullptr;\r\n"
    "    HWND max_queue_edit_ = nullptr;\r\n"
    "    HWND self_managed_check_ = nullptr;\r\n"
    "    HWND bypass_check_ = nullptr;\r\n};\r\n"
    "\r\n"
    "struct ProviderParsedUrl {"
)

# Try multiple variants
replaced = False
for old, new in [
    (old_members, new_members),
    (
        old_members.replace('\r\n', '\n'),
        new_members.replace('\r\n', '\n')
    ),
    (
        "    HWND self_managed_check_ = nullptr;\n};\n\nstruct ProviderParsedUrl {",
        "    HWND self_managed_check_ = nullptr;\n    HWND bypass_check_ = nullptr;\n};\n\nstruct ProviderParsedUrl {"
    ),
]:
    if old in text:
        text = text.replace(old, new)
        replaced = True
        break

if not replaced:
    print('WARN: Could not fix bypass_check_ member in provider_manager.cpp')
else:
    write_utf8('src/provider_manager.cpp', text, enc)
    print('Fixed bypass_check_ member')

# ============================================================
# Fix 2: openai_client.cpp – move ResolveBindingTarget after gate helpers
# ============================================================
text2, enc2 = read_utf8('src/openai_client.cpp')

# Find the broken top-namespace block and remove it
# It starts with "namespace {" then IsBindingProvider then ResolveBindingTarget then ends before "bool IsOllamaLocalProvider"
# We'll remove everything from "namespace {" before IsOllamaLocalProvider through "bool IsOllamaLocalProvider"
old_block = (
    "namespace {\r\n"
    "static bool IsBindingProvider(const ProviderConfig& provider) {\r\n"
    "    return NormalizeProviderType(provider.provider_type) == \"binding_provider\";\r\n"
    "}\r\n"
    "\r\n"
    "bool ResolveBindingTarget(const std::vector<ProviderConfig>& providers,\r\n"
    "                          const ModelConfig& binding_model,\r\n"
    "                          const ChatRequestOptions& original_request,\r\n"
    "                          ChatRequestOptions* out_request,\r\n"
    "                          const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/) {\r\n"
    "    if (!binding_model.is_binding_model || binding_model.binding_targets.empty()) return false;\r\n"
    "    const bool round_robin = binding_model.binding_routing_mode == BindingRoutingMode::RoundRobin;\r\n"
    "    // Cooldown check inline\r\n"
    "    auto is_cooldown = [&](const BindingTargetConfig& t) -> bool {\r\n"
    "        return OpenAIClient::BindingCooldown(t.provider_id, t.model_id);\r\n"
    "    };\r\n"
    "    std::vector<const BindingTargetConfig*> candidates;\r\n"
    "    for (const auto& t : binding_model.binding_targets) {\r\n"
    "        if (!is_cooldown(t)) candidates.push_back(&t);\r\n"
    "    }\r\n"
    "    if (candidates.empty()) {\r\n"
    "        for (const auto& t : binding_model.binding_targets) {\r\n"
    "            candidates.push_back(&t);\r\n"
    "        }\r\n"
    "    }\r\n"
    "    const BindingTargetConfig* selected = nullptr;\r\n"
    "    if (round_robin) {\r\n"
    "        static std::unordered_map<std::string, size_t> rr_index;\r\n"
    "        static std::mutex rr_mtx;\r\n"
    "        std::lock_guard<std::mutex> lock(rr_mtx);\r\n"
    "        size_t& idx = rr_index[binding_model.id];\r\n"
    "        for (size_t attempt = 0; attempt < candidates.size(); ++attempt) {\r\n"
    "            const auto* candidate = candidates[idx % candidates.size()];\r\n"
    "            idx++;\r\n"
    "            GateKey key{candidate->provider_id, candidate->model_id, GateDomain::Chat};\r\n"
    "            int provider_max_active = 0;\r\n"
    "            int provider_max_queue = 0;\r\n"
    "            int model_max_active = 0;\r\n"
    "            int model_max_queue = 0;\r\n"
    "            bool self_managed = false;\r\n"
    "            {\r\n"
    "                std::lock_guard<std::mutex> map_lock(GateMapMutex());\r\n"
    "                auto it = s_gate_provider_cache.find(candidate->provider_id);\r\n"
    "                if (it != s_gate_provider_cache.end()) {\r\n"
    "                    provider_max_active = it->second.max_active_requests;\r\n"
    "                    provider_max_queue = it->second.max_queue_size;\r\n"
    "                    for (const auto& m : it->second.models) {\r\n"
    "                        if (m.id == candidate->model_id) {\r\n"
    "                            model_max_active = m.max_active_requests;\r\n"
    "                            model_max_queue = m.max_queue_size;\r\n"
    "                            self_managed = m.self_managed_queue;\r\n"
    "                            break;\r\n"
    "                        }\r\n"
    "                    }\r\n"
    "                }\r\n"
    "            }\r\n"
    "            const int effective_max_active = ComputeEffectiveMaxActive(provider_max_active, model_max_active);\r\n"
    "            const int effective_max_queue = ComputeEffectiveMaxQueue(provider_max_queue, model_max_queue);\r\n"
    "            if (self_managed) {\r\n"
    "                selected = candidate;\r\n"
    "                break;\r\n"
    "            }\r\n"
    "            if (ProviderRequestGate::Acquire(key, effective_max_active, effective_max_queue, on_queue_status)) {\r\n"
    "                selected = candidate;\r\n"
    "                break;\r\n"
    "            }\r\n"
    "        }\r\n"
    "    } else {\r\n"
    "        for (const auto* candidate : candidates) {\r\n"
    "            GateKey key{candidate->provider_id, candidate->model_id, GateDomain::Chat};\r\n"
    "            int provider_max_active = 0;\r\n"
    "            int provider_max_queue = 0;\r\n"
    "            int model_max_active = 0;\r\n"
    "            int model_max_queue = 0;\r\n"
    "            bool self_managed = false;\r\n"
    "            {\r\n"
    "                std::lock_guard<std::mutex> map_lock(GateMapMutex());\r\n"
    "                auto it = s_gate_provider_cache.find(candidate->provider_id);\r\n"
    "                if (it != s_gate_provider_cache.end()) {\r\n"
    "                    provider_max_active = it->second.max_active_requests;\r\n"
    "                    provider_max_queue = it->second.max_queue_size;\r\n"
    "                    for (const auto& m : it->second.models) {\r\n"
    "                        if (m.id == candidate->model_id) {\r\n"
    "                            model_max_active = m.max_active_requests;\r\n"
    "                            model_max_queue = m.max_queue_size;\r\n"
    "                            self_managed = m.self_managed_queue;\r\n"
    "                            break;\r\n"
    "                        }\r\n"
    "                    }\r\n"
    "                }\r\n"
    "            }\r\n"
    "            const int effective_max_active = ComputeEffectiveMaxActive(provider_max_active, model_max_active);\r\n"
    "            const int effective_max_queue = ComputeEffectiveMaxQueue(provider_max_queue, model_max_queue);\r\n"
    "            if (self_managed) {\r\n"
    "                selected = candidate;\r\n"
    "                break;\r\n"
    "            }\r\n"
    "            if (ProviderRequestGate::Acquire(key, effective_max_active, effective_max_queue, on_queue_status)) {\r\n"
    "                selected = candidate;\r\n"
    "                break;\r\n"
    "            }\r\n"
    "        }\r\n"
    "    }\r\n"
    "    if (!selected) return false;\r\n"
    "    // Resolve provider/model from global provider list\r\n"
    "    for (const auto& p : providers) {\r\n"
    "        if (p.id != selected->provider_id) continue;\r\n"
    "        for (const auto& m : p.models) {\r\n"
    "            if (m.id == selected->model_id) {\r\n"
    "                out_request->provider = p;\r\n"
    "                out_request->model = m;\r\n"
    "                out_request->system_prompt = original_request.system_prompt;\r\n"
    "                out_request->temperature = original_request.temperature;\r\n"
    "                out_request->max_tokens = original_request.max_tokens;\r\n"
    "                out_request->messages = original_request.messages;\r\n"
    "                out_request->binding_model_id = binding_model.id;\r\n"
    "                out_request->binding_depth = original_request.binding_depth + 1;\r\n"
    "                return true;\r\n"
    "            }\r\n"
    "        }\r\n"
    "    }\r\n"
    "    return false;\r\n"
    "}\r\n"
    "\r\n"
    "bool IsOllamaLocalProvider"
)

if old_block in text2:
    text2 = text2.replace(old_block, "bool IsOllamaLocalProvider")
    print('Removed misplaced ResolveBindingTarget from openai_client.cpp')
else:
    print('WARN: Could not find old_block in openai_client.cpp')

# Insert new ResolveBindingTarget after GateSlot destructor
old_gateslot = (
    "GateSlot::~GateSlot() {\r\n"
    "    if (acquired) {\r\n"
    "        ProviderRequestGate::Release(key);\r\n"
    "    }\r\n"
    "}\r\n"
    "\r\n"
    "namespace {"
)
new_gateslot = (
    "GateSlot::~GateSlot() {\r\n"
    "    if (acquired) {\r\n"
    "        ProviderRequestGate::Release(key);\r\n"
    "    }\r\n"
    "}\r\n"
    "\r\n"
    "bool ResolveBindingTarget(const std::vector<ProviderConfig>& providers,\r\n"
    "                          const ModelConfig& binding_model,\r\n"
    "                          const ChatRequestOptions& original_request,\r\n"
    "                          ChatRequestOptions* out_request,\r\n"
    "                          const std::function<void(const ProviderQueueStatus&)>& on_queue_status) {\r\n"
    "    if (!binding_model.is_binding_model || binding_model.binding_targets.empty()) return false;\r\n"
    "    const bool round_robin = binding_model.binding_routing_mode == BindingRoutingMode::RoundRobin;\r\n"
    "    auto is_cooldown = [&](const BindingTargetConfig& t) -> bool {\r\n"
    "        return OpenAIClient::BindingCooldown(t.provider_id, t.model_id);\r\n"
    "    };\r\n"
    "    std::vector<const BindingTargetConfig*> candidates;\r\n"
    "    for (const auto& t : binding_model.binding_targets) {\r\n"
    "        if (!is_cooldown(t)) candidates.push_back(&t);\r\n"
    "    }\r\n"
    "    if (candidates.empty()) {\r\n"
    "        for (const auto& t : binding_model.binding_targets) {\r\n"
    "            candidates.push_back(&t);\r\n"
    "        }\r\n"
    "    }\r\n"
    "    const BindingTargetConfig* selected = nullptr;\r\n"
    "    if (round_robin) {\r\n"
    "        static std::unordered_map<std::string, size_t> rr_index;\r\n"
    "        static std::mutex rr_mtx;\r\n"
    "        std::lock_guard<std::mutex> lock(rr_mtx);\r\n"
    "        size_t& idx = rr_index[binding_model.id];\r\n"
    "        for (size_t attempt = 0; attempt < candidates.size(); ++attempt) {\r\n"
    "            const auto* candidate = candidates[idx % candidates.size()];\r\n"
    "            idx++;\r\n"
    "            GateKey key{candidate->provider_id, candidate->model_id, GateDomain::Chat};\r\n"
    "            int p_active = 0, p_queue = 0, m_active = 0, m_queue = 0;\r\n"
    "            bool self_managed = false;\r\n"
    "            {\r\n"
    "                std::lock_guard<std::mutex> map_lock(GateMapMutex());\r\n"
    "                auto it = s_gate_provider_cache.find(candidate->provider_id);\r\n"
    "                if (it != s_gate_provider_cache.end()) {\r\n"
    "                    p_active = it->second.max_active_requests;\r\n"
    "                    p_queue = it->second.max_queue_size;\r\n"
    "                    for (const auto& m : it->second.models) {\r\n"
    "                        if (m.id == candidate->model_id) {\r\n"
    "                            m_active = m.max_active_requests;\r\n"
    "                            m_queue = m.max_queue_size;\r\n"
    "                            self_managed = m.self_managed_queue;\r\n"
    "                            break;\r\n"
    "                        }\r\n"
    "                    }\r\n"
    "                }\r\n"
    "            }\r\n"
    "            int eff_active = ComputeEffectiveMaxActive(p_active, m_active);\r\n"
    "            int eff_queue = ComputeEffectiveMaxQueue(p_queue, m_queue);\r\n"
    "            if (self_managed) {\r\n"
    "                selected = candidate;\r\n"
    "                break;\r\n"
    "            }\r\n"
    "            if (ProviderRequestGate::Acquire(key, eff_active, eff_queue, on_queue_status)) {\r\n"
    "                selected = candidate;\r\n"
    "                break;\r\n"
    "            }\r\n"
    "        }\r\n"
    "    } else {\r\n"
    "        for (const auto* candidate : candidates) {\r\n"
    "            GateKey key{candidate->provider_id, candidate->model_id, GateDomain::Chat};\r\n"
    "            int p_active = 0, p_queue = 0, m_active = 0, m_queue = 0;\r\n"
    "            bool self_managed = false;\r\n"
    "            {\r\n"
    "                std::lock_guard<std::mutex> map_lock(GateMapMutex());\r\n"
    "                auto it = s_gate_provider_cache.find(candidate->provider_id);\r\n"
    "                if (it != s_gate_provider_cache.end()) {\r\n"
    "                    p_active = it->second.max_active_requests;\r\n"
    "                    p_queue = it->second.max_queue_size;\r\n"
    "                    for (const auto& m : it->second.models) {\r\n"
    "                        if (m.id == candidate->model_id) {\r\n"
    "                            m_active = m.max_active_requests;\r\n"
    "                            m_queue = m.max_queue_size;\r\n"
    "                            self_managed = m.self_managed_queue;\r\n"
    "                            break;\r\n"
    "                        }\r\n"
    "                    }\r\n"
    "                }\r\n"
    "            }\r\n"
    "            int eff_active = ComputeEffectiveMaxActive(p_active, m_active);\r\n"
    "            int eff_queue = ComputeEffectiveMaxQueue(p_queue, m_queue);\r\n"
    "            if (self_managed) {\r\n"
    "                selected = candidate;\r\n"
    "                break;\r\n"
    "            }\r\n"
    "            if (ProviderRequestGate::Acquire(key, eff_active, eff_queue, on_queue_status)) {\r\n"
    "                selected = candidate;\r\n"
    "                break;\r\n"
    "            }\r\n"
    "        }\r\n"
    "    }\r\n"
    "    if (!selected) return false;\r\n"
    "    for (const auto& p : providers) {\r\n"
    "        if (p.id != selected->provider_id) continue;\r\n"
    "        for (const auto& m : p.models) {\r\n"
    "            if (m.id == selected->model_id) {\r\n"
    "                out_request->provider = p;\r\n"
    "                out_request->model = m;\r\n"
    "                out_request->system_prompt = original_request.system_prompt;\r\n"
    "                out_request->temperature = original_request.temperature;\r\n"
    "                out_request->max_tokens = original_request.max_tokens;\r\n"
    "                out_request->messages = original_request.messages;\r\n"
    "                out_request->binding_model_id = binding_model.id;\r\n"
    "                out_request->binding_depth = original_request.binding_depth + 1;\r\n"
    "                return true;\r\n"
    "            }\r\n"
    "        }\r\n"
    "    }\r\n"
    "    return false;\r\n"
    "}\r\n"
    "\r\n"
    "namespace {"
)

if old_gateslot in text2:
    text2 = text2.replace(old_gateslot, new_gateslot)
    print('Inserted ResolveBindingTarget after GateSlot destructor')
else:
    print('WARN: Could not find GateSlot destructor insertion point')

write_utf8('src/openai_client.cpp', text2, enc2)
print('Fixed openai_client.cpp')
print('Done.')
