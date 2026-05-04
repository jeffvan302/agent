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
# 1. provider_manager.cpp – add queue UI controls to ModelEditorDialog
# ============================================================
text, enc = read_utf8('src/provider_manager.cpp')

# 1a. Add control IDs after kModelEditorSelfManagedCheck = 2345
old = "    kModelEditorSelfManagedCheck = 2345,\n    kModelEditorSave = IDOK,"
new = "    kModelEditorSelfManagedCheck = 2345,\n    kModelEditorBypassCheck = 2346,\n    kModelEditorSave = IDOK,"
if old in text:
    text = text.replace(old, new)
else:
    print('WARN: control ID enum old not found')

# 1b. Add OnCreate creation of queue controls (after thinking_check_ creation)
old_create = (
    "        thinking_check_ = CreateButton(L\"Thinking capable\", kModelEditorThinkingCheck, BS_AUTOCHECKBOX);\n"
    "        max_completion_check_ = CreateButton(L\"Prefer max_completion_tokens\", kModelEditorMaxCompletionCheck, BS_AUTOCHECKBOX);"
)
new_create = (
    "        thinking_check_ = CreateButton(L\"Thinking capable\", kModelEditorThinkingCheck, BS_AUTOCHECKBOX);\n"
    "        max_active_label_ = CreateLabel(L\"Max active (0=default):\", kModelEditorMaxActiveLabel);\n"
    "        max_active_edit_ = CreateEdit(model_.max_active_requests > 0 ? std::to_wstring(model_.max_active_requests).c_str() : L\"\", kModelEditorMaxActiveEdit, ES_NUMBER);\n"
    "        max_queue_label_ = CreateLabel(L\"Max queue (0=default):\", kModelEditorMaxQueueLabel);\n"
    "        max_queue_edit_ = CreateEdit(model_.max_queue_size > 0 ? std::to_wstring(model_.max_queue_size).c_str() : L\"\", kModelEditorMaxQueueEdit, ES_NUMBER);\n"
    "        self_managed_check_ = CreateButton(L\"Self-managed queue\", kModelEditorSelfManagedCheck, BS_AUTOCHECKBOX);\n"
    "        Button_SetCheck(self_managed_check_, model_.self_managed_queue ? BST_CHECKED : BST_UNCHECKED);\n"
    "        bypass_check_ = CreateButton(L\"Bypass queue (binding direct call)\", kModelEditorBypassCheck, BS_AUTOCHECKBOX);\n"
    "        Button_SetCheck(bypass_check_, model_.bypass_queue ? BST_CHECKED : BST_UNCHECKED);\n"
    "        max_completion_check_ = CreateButton(L\"Prefer max_completion_tokens\", kModelEditorMaxCompletionCheck, BS_AUTOCHECKBOX);"
)
if old_create in text:
    text = text.replace(old_create, new_create)
else:
    print('WARN: OnCreate old not found')

# 1c. Add controls to font list and initial visibility
old_font = (
    "        for (HWND control : {provider_label_, provider_combo_, id_label_, id_edit_, ollama_load_info_button_, ollama_search_button_, display_label_, display_edit_, context_label_, context_edit_, ollama_timeout_label_, ollama_timeout_edit_, ollama_num_threads_label_, ollama_num_threads_edit_, ollama_no_gpu_check_, ollama_gpu_layers_label_, ollama_gpu_layers_edit_, ollama_ctx_len_label_, ollama_ctx_len_edit_, ollama_verbose_check_, streaming_check_, tools_check_, vision_check_, embedding_check_, thinking_check_, max_completion_check_, reasoning_label_, reasoning_combo_, verbosity_label_, verbosity_combo_, routing_label_, routing_combo_, targets_label_, targets_list_, add_target_button_, edit_target_button_, remove_target_button_, move_target_up_button_, move_target_down_button_, save_button_, cancel_button_}) {"
)
new_font = (
    "        for (HWND control : {provider_label_, provider_combo_, id_label_, id_edit_, ollama_load_info_button_, ollama_search_button_, display_label_, display_edit_, context_label_, context_edit_, ollama_timeout_label_, ollama_timeout_edit_, ollama_num_threads_label_, ollama_num_threads_edit_, ollama_no_gpu_check_, ollama_gpu_layers_label_, ollama_gpu_layers_edit_, ollama_ctx_len_label_, ollama_ctx_len_edit_, ollama_verbose_check_, streaming_check_, tools_check_, vision_check_, embedding_check_, thinking_check_, max_active_label_, max_active_edit_, max_queue_label_, max_queue_edit_, self_managed_check_, bypass_check_, max_completion_check_, reasoning_label_, reasoning_combo_, verbosity_label_, verbosity_combo_, routing_label_, routing_combo_, targets_label_, targets_list_, add_target_button_, edit_target_button_, remove_target_button_, move_target_up_button_, move_target_down_button_, save_button_, cancel_button_}) {"
)
if old_font in text:
    text = text.replace(old_font, new_font)
else:
    print('WARN: font loop old not found')

# 1d. Add layout entries after thinking_check_ layout block
old_layout = (
    "        MoveWindow(thinking_check_, margin + label_width, y, Scale(180), edit_height, TRUE);\n"
    "        y += edit_height + gutter;\n"
    "        MoveWindow(max_completion_check_, margin + label_width, y, Scale(220), edit_height, TRUE);"
)
new_layout = (
    "        MoveWindow(thinking_check_, margin + label_width, y, Scale(180), edit_height, TRUE);\n"
    "        y += edit_height + gutter;\n"
    "        MoveWindow(max_active_label_, margin, y + Scale(4), label_width, label_height, TRUE);\n"
    "        MoveWindow(max_active_edit_, margin + label_width, y, Scale(90), edit_height, TRUE);\n"
    "        MoveWindow(max_queue_label_, margin + label_width + Scale(100), y + Scale(4), Scale(110), label_height, TRUE);\n"
    "        MoveWindow(max_queue_edit_, margin + label_width + Scale(212), y, Scale(90), edit_height, TRUE);\n"
    "        y += edit_height + gutter;\n"
    "        MoveWindow(self_managed_check_, margin + label_width, y, Scale(180), edit_height, TRUE);\n"
    "        MoveWindow(bypass_check_, margin + label_width + Scale(200), y, Scale(260), edit_height, TRUE);\n"
    "        y += edit_height + gutter;\n"
    "        MoveWindow(max_completion_check_, margin + label_width, y, Scale(220), edit_height, TRUE);"
)
if old_layout in text:
    text = text.replace(old_layout, new_layout)
else:
    print('WARN: layout old not found')

# 1e. Read values in ValidateAndSave
old_save = (
    "        model.supports_embedding = Button_GetCheck(embedding_check_) == BST_CHECKED;\n"
    "        model.supports_thinking = Button_GetCheck(thinking_check_) == BST_CHECKED;"
)
new_save = (
    "        model.supports_embedding = Button_GetCheck(embedding_check_) == BST_CHECKED;\n"
    "        model.supports_thinking = Button_GetCheck(thinking_check_) == BST_CHECKED;\n"
    "        model.max_active_requests = std::max(0, ParseInt(TrimWide(GetWindowTextString(max_active_edit_))).value_or(0));\n"
    "        model.max_queue_size = std::max(0, ParseInt(TrimWide(GetWindowTextString(max_queue_edit_))).value_or(0));\n"
    "        model.self_managed_queue = Button_GetCheck(self_managed_check_) == BST_CHECKED;\n"
    "        model.bypass_queue = Button_GetCheck(bypass_check_) == BST_CHECKED;"
)
if old_save in text:
    text = text.replace(old_save, new_save)
else:
    print('WARN: ValidateAndSave old not found')

# 1f. Add member HWNDs at end of ModelEditorDialog
old_members = (
    "    HWND max_active_label_ = nullptr;\n"
    "    HWND max_active_edit_ = nullptr;\n"
    "    HWND max_queue_label_ = nullptr;\n"
    "    HWND max_queue_edit_ = nullptr;\n"
    "    HWND self_managed_check_ = nullptr;\n};\n"
    "\nstruct ProviderParsedUrl {"
)
new_members = (
    "    HWND max_active_label_ = nullptr;\n"
    "    HWND max_active_edit_ = nullptr;\n"
    "    HWND max_queue_label_ = nullptr;\n"
    "    HWND max_queue_edit_ = nullptr;\n"
    "    HWND self_managed_check_ = nullptr;\n"
    "    HWND bypass_check_ = nullptr;\n};\n"
    "\nstruct ProviderParsedUrl {"
)
if old_members in text:
    text = text.replace(old_members, new_members)
else:
    print('WARN: member old not found')

write_utf8('src/provider_manager.cpp', text, enc)
print('Updated provider_manager.cpp')

# ============================================================
# 2. openai_client.cpp – binding resolution + bypass
# ============================================================
text2, enc2 = read_utf8('src/openai_client.cpp')

# 2a. Add ResolveBindingTarget before the first binding_model reference
# We'll insert before IsOllamaLocalProvider definition at line 29.
old_ollamadef = "bool IsOllamaLocalProvider(const ProviderConfig& provider) {"

new_ollamadef = (
    "static bool IsBindingProvider(const ProviderConfig& provider) {\n"
    "    return NormalizeProviderType(provider.provider_type) == \"binding_provider\";\n"
    "}\n"
    "\n"
    "bool ResolveBindingTarget(const std::vector<ProviderConfig>& providers,\n"
    "                          const ModelConfig& binding_model,\n"
    "                          const ChatRequestOptions& original_request,\n"
    "                          ChatRequestOptions* out_request,\n"
    "                          const std::function<void(const ProviderQueueStatus&)>& on_queue_status) {\n"
    "    if (!binding_model.is_binding_model || binding_model.binding_targets.empty()) return false;\n"
    "    const bool round_robin = binding_model.binding_routing_mode == BindingRoutingMode::RoundRobin;\n"
    "    // Cooldown check inline\n"
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
    "            GateKey key{candidate->provider_id, candidate->model_id, GateDomain::Chat};\n"
    "            int provider_max_active = 0;\n"
    "            int provider_max_queue = 0;\n"
    "            int model_max_active = 0;\n"
    "            int model_max_queue = 0;\n"
    "            bool self_managed = false;\n"
    "            {\n"
    "                std::lock_guard<std::mutex> map_lock(GateMapMutex());\n"
    "                auto it = s_gate_provider_cache.find(candidate->provider_id);\n"
    "                if (it != s_gate_provider_cache.end()) {\n"
    "                    provider_max_active = it->second.max_active_requests;\n"
    "                    provider_max_queue = it->second.max_queue_size;\n"
    "                    for (const auto& m : it->second.models) {\n"
    "                        if (m.id == candidate->model_id) {\n"
    "                            model_max_active = m.max_active_requests;\n"
    "                            model_max_queue = m.max_queue_size;\n"
    "                            self_managed = m.self_managed_queue;\n"
    "                            break;\n"
    "                        }\n"
    "                    }\n"
    "                }\n"
    "            }\n"
    "            const int effective_max_active = ComputeEffectiveMaxActive(provider_max_active, model_max_active);\n"
    "            const int effective_max_queue = ComputeEffectiveMaxQueue(provider_max_queue, model_max_queue);\n"
    "            if (self_managed) {\n"
    "                selected = candidate;\n"
    "                break;\n"
    "            }\n"
    "            if (ProviderRequestGate::Acquire(key, effective_max_active, effective_max_queue, on_queue_status)) {\n"
    "                selected = candidate;\n"
    "                break;\n"
    "            }\n"
    "        }\n"
    "    } else {\n"
    "        for (const auto* candidate : candidates) {\n"
    "            GateKey key{candidate->provider_id, candidate->model_id, GateDomain::Chat};\n"
    "            int provider_max_active = 0;\n"
    "            int provider_max_queue = 0;\n"
    "            int model_max_active = 0;\n"
    "            int model_max_queue = 0;\n"
    "            bool self_managed = false;\n"
    "            {\n"
    "                std::lock_guard<std::mutex> map_lock(GateMapMutex());\n"
    "                auto it = s_gate_provider_cache.find(candidate->provider_id);\n"
    "                if (it != s_gate_provider_cache.end()) {\n"
    "                    provider_max_active = it->second.max_active_requests;\n"
    "                    provider_max_queue = it->second.max_queue_size;\n"
    "                    for (const auto& m : it->second.models) {\n"
    "                        if (m.id == candidate->model_id) {\n"
    "                            model_max_active = m.max_active_requests;\n"
    "                            model_max_queue = m.max_queue_size;\n"
    "                            self_managed = m.self_managed_queue;\n"
    "                            break;\n"
    "                        }\n"
    "                    }\n"
    "                }\n"
    "            }\n"
    "            const int effective_max_active = ComputeEffectiveMaxActive(provider_max_active, model_max_active);\n"
    "            const int effective_max_queue = ComputeEffectiveMaxQueue(provider_max_queue, model_max_queue);\n"
    "            if (self_managed) {\n"
    "                selected = candidate;\n"
    "                break;\n"
    "            }\n"
    "            if (ProviderRequestGate::Acquire(key, effective_max_active, effective_max_queue, on_queue_status)) {\n"
    "                selected = candidate;\n"
    "                break;\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    if (!selected) return false;\n"
    "    // Resolve provider/model from global provider list\n"
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
    "bool IsOllamaLocalProvider(const ProviderConfig& provider) {"
)

if old_ollamadef in text2:
    text2 = text2.replace(old_ollamadef, new_ollamadef)
else:
    print('WARN: IsOllamaLocalProvider def not found')

# 2b. Add #include <unordered_map> if missing
if '#include <unordered_map>' not in text2:
    text2 = text2.replace('#include <thread>', '#include <thread>\n#include <unordered_map>')

# 2c. Add #include "types.h" if missing from openai_client.cpp (it already includes types.h via openai_client.h, but we need BindingRoutingMode)
# Actually BindingRoutingMode is in types.h, which is already included. Good.

# 2d. Wire StreamChat with binding bypass
old_streamchat = (
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
    "    try {\n"
    "        if (IsOllamaLocalProvider(request.provider)) {"
)
new_streamchat = (
    "ChatExecutionResult OpenAIClient::StreamChat(const ChatRequestOptions& request,\n"
    "                                           const std::function<void(const std::string&)>& on_delta,\n"
    "                                           const std::function<void(const ProviderQueueStatus&)>& on_queue_status,\n"
    "                                           const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {\n"
    "    // Binding bypass: resolve to concrete target and acquire its gate directly\n"
    "    if (request.model.is_binding_model && request.model.bypass_queue && request.binding_depth < 8) {\n"
    "        ChatRequestOptions resolved = request;\n"
    "        std::vector<ProviderConfig> all_providers;\n"
    "        {\n"
    "            std::lock_guard<std::mutex> lock(GateMapMutex());\n"
    "            for (const auto& kv : s_gate_provider_cache) all_providers.push_back(kv.second);\n"
    "        }\n"
    "        if (ResolveBindingTarget(all_providers, request.model, request, &resolved, on_queue_status)) {\n"
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
    "    try {\n"
    "        if (IsOllamaLocalProvider(request.provider)) {"
)
if old_streamchat in text2:
    text2 = text2.replace(old_streamchat, new_streamchat)
else:
    print('WARN: StreamChat old not found')

# 2e. Wire CreateToolAwareCompletion with binding bypass
old_tool = (
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
new_tool = (
    "ChatCompletionResult OpenAIClient::CreateToolAwareCompletion(const ChatRequestOptions& request,\n"
    "                                                           const std::vector<ChatToolDefinition>& tools,\n"
    "                                                           const std::function<void(const ProviderQueueStatus&)>& on_queue_status,\n"
    "                                                           const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {\n"
    "    if (request.model.is_binding_model && request.model.bypass_queue && request.binding_depth < 8) {\n"
    "        ChatRequestOptions resolved = request;\n"
    "        std::vector<ProviderConfig> all_providers;\n"
    "        {\n"
    "            std::lock_guard<std::mutex> lock(GateMapMutex());\n"
    "            for (const auto& kv : s_gate_provider_cache) all_providers.push_back(kv.second);\n"
    "        }\n"
    "        if (ResolveBindingTarget(all_providers, request.model, request, &resolved, on_queue_status)) {\n"
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
if old_tool in text2:
    text2 = text2.replace(old_tool, new_tool)
else:
    print('WARN: CreateToolAwareCompletion old not found')

# 2f. Wire CreateSimpleCompletion with binding bypass
old_simple = (
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
new_simple = (
    "ChatCompletionResult OpenAIClient::CreateSimpleCompletion(const ChatRequestOptions& request,\n"
    "                                                        const std::function<void(const ProviderQueueStatus&)>& on_queue_status,\n"
    "                                                        const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {\n"
    "    if (request.model.is_binding_model && request.model.bypass_queue && request.binding_depth < 8) {\n"
    "        ChatRequestOptions resolved = request;\n"
    "        std::vector<ProviderConfig> all_providers;\n"
    "        {\n"
    "            std::lock_guard<std::mutex> lock(GateMapMutex());\n"
    "            for (const auto& kv : s_gate_provider_cache) all_providers.push_back(kv.second);\n"
    "        }\n"
    "        if (ResolveBindingTarget(all_providers, request.model, request, &resolved, on_queue_status)) {\n"
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
if old_simple in text2:
    text2 = text2.replace(old_simple, new_simple)
else:
    print('WARN: CreateSimpleCompletion old not found')

# 2g. Wire StreamToolAwareCompletion with binding bypass
old_streamtool = (
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
new_streamtool = (
    "ChatCompletionResult OpenAIClient::StreamToolAwareCompletion(const ChatRequestOptions& request,\n"
    "                                                             const std::vector<ChatToolDefinition>& tools,\n"
    "                                                             const std::function<void(const std::string&)>& on_delta,\n"
    "                                                             const std::function<void(const ProviderQueueStatus&)>& on_queue_status,\n"
    "                                                             const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {\n"
    "    if (request.model.is_binding_model && request.model.bypass_queue && request.binding_depth < 8) {\n"
    "        ChatRequestOptions resolved = request;\n"
    "        std::vector<ProviderConfig> all_providers;\n"
    "        {\n"
    "            std::lock_guard<std::mutex> lock(GateMapMutex());\n"
    "            for (const auto& kv : s_gate_provider_cache) all_providers.push_back(kv.second);\n"
    "        }\n"
    "        if (ResolveBindingTarget(all_providers, request.model, request, &resolved, on_queue_status)) {\n"
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
if old_streamtool in text2:
    text2 = text2.replace(old_streamtool, new_streamtool)
else:
    print('WARN: StreamToolAwareCompletion old not found')

# 2h. Wire CreateEmbedding with dispatch + binding bypass
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
    "\n"
    "    // Binding bypass for embeddings: resolve to concrete target and acquire its gate directly\n"
    "    if (model.is_binding_model && model.bypass_queue) {\n"
    "        // Build a synthetic ChatRequestOptions for resolution\n"
    "        ChatRequestOptions synthetic;\n"
    "        synthetic.provider = provider;\n"
    "        synthetic.model = model;\n"
    "        ChatRequestOptions resolved = synthetic;\n"
    "        std::vector<ProviderConfig> all_providers;\n"
    "        {\n"
    "            std::lock_guard<std::mutex> lock(GateMapMutex());\n"
    "            for (const auto& kv : s_gate_provider_cache) all_providers.push_back(kv.second);\n"
    "        }\n"
    "        // Note: binding_targets for embeddings should point to embedding-capable models\n"
    "        // For now we reuse the same ResolveBindingTarget; the caller ensures target supports embedding.\n"
    "        if (!ResolveBindingTarget(all_providers, model, synthetic, &resolved, on_queue_status)) {\n"
    "            throw std::runtime_error(\"Embedding binding resolution failed for \" + model.id);\n"
    "        }\n"
    "        return CreateEmbedding(resolved.provider, resolved.model, texts, on_queue_status);\n"
    "    }\n"
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
    "    // Dispatch to provider-specific embedding path\n"
    "    const std::string ptype = NormalizeProviderType(provider.provider_type);\n"
    "    if (ptype == \"ollama_local\") {\n"
    "        return RunOllamaLocalEmbedding(provider, model, texts, on_queue_status);\n"
    "    }\n"
    "    if (ptype == \"ollama_cloud\") {\n"
    "        return RunOllamaCloudEmbedding(provider, model, texts, on_queue_status);\n"
    "    }\n"
    "    return RunOpenAICompatibleEmbedding(provider, model, texts, on_queue_status);\n"
    "}"
)
if old_embed in text2:
    text2 = text2.replace(old_embed, new_embed)
else:
    print('WARN: CreateEmbedding old not found')

write_utf8('src/openai_client.cpp', text2, enc2)
print('Updated openai_client.cpp')

# ============================================================
# 3. openai_client.h – add ResolveBindingTarget + embedding dispatch declarations
# ============================================================
text3, enc3 = read_utf8('src/openai_client.h')

# Add forward declarations before class OpenAIClient
old_class = "class OpenAIClient {"
new_class = (
    "struct BindingTargetConfig;\n"
    "enum class BindingRoutingMode;\n"
    "\n"
    "bool ResolveBindingTarget(const std::vector<ProviderConfig>& providers,\n"
    "                          const ModelConfig& binding_model,\n"
    "                          const ChatRequestOptions& original_request,\n"
    "                          ChatRequestOptions* out_request,\n"
    "                          const std::function<void(const ProviderQueueStatus&)>& on_queue_status);\n"
    "\n"
    "std::vector<std::vector<float>> RunOllamaLocalEmbedding(const ProviderConfig& provider,\n"
    "                                                         const ModelConfig& model,\n"
    "                                                         const std::vector<std::string>& texts,\n"
    "                                                         const std::function<void(const ProviderQueueStatus&)>& on_queue_status);\n"
    "\n"
    "std::vector<std::vector<float>> RunOllamaCloudEmbedding(const ProviderConfig& provider,\n"
    "                                                         const ModelConfig& model,\n"
    "                                                         const std::vector<std::string>& texts,\n"
    "                                                         const std::function<void(const ProviderQueueStatus&)>& on_queue_status);\n"
    "\n"
    "std::vector<std::vector<float>> RunOpenAICompatibleEmbedding(const ProviderConfig& provider,\n"
    "                                                             const ModelConfig& model,\n"
    "                                                             const std::vector<std::string>& texts,\n"
    "                                                             const std::function<void(const ProviderQueueStatus&)>& on_queue_status);\n"
    "\n"
    "class OpenAIClient {"
)
if old_class in text3:
    text3 = text3.replace(old_class, new_class)
else:
    print('WARN: class OpenAIClient not found in header')

write_utf8('src/openai_client.h', text3, enc3)
print('Updated openai_client.h')

# ============================================================
# 4. types.h – add bypass_queue field to ModelConfig
# ============================================================
text4, enc4 = read_utf8('src/types.h')
old_model = (
    "    int max_active_requests = 0;       // 0 = use provider default\n"
    "    int max_queue_size = 0;            // 0 = use provider default (0 at both levels means default gate = 1 active / 100 queue)\n"
    "    bool self_managed_queue = false;   // true = remote worker manages its own queue, local gate bypasses"
)
new_model = (
    "    int max_active_requests = 0;       // 0 = use provider default\n"
    "    int max_queue_size = 0;            // 0 = use provider default (0 at both levels means default gate = 1 active / 100 queue)\n"
    "    bool self_managed_queue = false;   // true = remote worker manages its own queue, local gate bypasses\n"
    "    bool bypass_queue = false;         // true = binding model skips its own gate and directly acquires target gate(s)"
)
if old_model in text4:
    text4 = text4.replace(old_model, new_model)
else:
    print('WARN: ModelConfig old not found')

write_utf8('src/types.h', text4, enc4)
print('Updated types.h')

# ============================================================
# 5. storage.cpp – persist bypass_queue
# ============================================================
text5, enc5 = read_utf8('src/storage.cpp')
old_storage = (
    "        {\"max_active_requests\", model.max_active_requests},\n"
    "        {\"max_queue_size\", model.max_queue_size},\n"
    "        {\"self_managed_queue\", model.self_managed_queue},"
)
new_storage = (
    "        {\"max_active_requests\", model.max_active_requests},\n"
    "        {\"max_queue_size\", model.max_queue_size},\n"
    "        {\"self_managed_queue\", model.self_managed_queue},\n"
    "        {\"bypass_queue\", model.bypass_queue},"
)
if old_storage in text5:
    text5 = text5.replace(old_storage, new_storage)
else:
    print('WARN: storage serialization old not found')

old_storage2 = (
    "    model.max_active_requests = std::max(0, item.value(\"max_active_requests\", 0));\n"
    "    model.max_queue_size = std::max(0, item.value(\"max_queue_size\", 0));\n"
    "    model.self_managed_queue = item.value(\"self_managed_queue\", false);"
)
new_storage2 = (
    "    model.max_active_requests = std::max(0, item.value(\"max_active_requests\", 0));\n"
    "    model.max_queue_size = std::max(0, item.value(\"max_queue_size\", 0));\n"
    "    model.self_managed_queue = item.value(\"self_managed_queue\", false);\n"
    "    model.bypass_queue = item.value(\"bypass_queue\", false);"
)
if old_storage2 in text5:
    text5 = text5.replace(old_storage2, new_storage2)
else:
    print('WARN: storage deserialization old not found')

write_utf8('src/storage.cpp', text5, enc5)
print('Updated storage.cpp')

# ============================================================
# 6. ollama_api_client.cpp – add embedding dispatch stubs
# ============================================================
# Since we declared RunOllamaLocalEmbedding etc. in openai_client.h,
# we must provide at least stub definitions so the linker doesn't complain.
# For now they can live in openai_client.cpp after CreateEmbedding.
text6, enc6 = read_utf8('src/openai_client.cpp')

old_after_embed = (
    "    return RunOpenAICompatibleEmbedding(provider, model, texts, on_queue_status);\n"
    "}\n"
    "\n"
    "static std::unordered_map<std::string, int64_t> g_cooldown_end_sec; // key = \"provider_id|model_id\""
)
new_after_embed = (
    "    return RunOpenAICompatibleEmbedding(provider, model, texts, on_queue_status);\n"
    "}\n"
    "\n"
    "std::vector<std::vector<float>> RunOllamaLocalEmbedding(const ProviderConfig& provider,\n"
    "                                                         const ModelConfig& model,\n"
    "                                                         const std::vector<std::string>& texts,\n"
    "                                                         const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/) {\n"
    "    // TODO: implement Ollama /api/embed batch call\n"
    "    (void)provider; (void)model; (void)texts;\n"
    "    throw std::runtime_error(\"RunOllamaLocalEmbedding not yet implemented.\");\n"
    "}\n"
    "\n"
    "std::vector<std::vector<float>> RunOllamaCloudEmbedding(const ProviderConfig& provider,\n"
    "                                                         const ModelConfig& model,\n"
    "                                                         const std::vector<std::string>& texts,\n"
    "                                                         const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/) {\n"
    "    // TODO: implement Ollama Cloud /api/embed batch call\n"
    "    (void)provider; (void)model; (void)texts;\n"
    "    throw std::runtime_error(\"RunOllamaCloudEmbedding not yet implemented.\");\n"
    "}\n"
    "\n"
    "std::vector<std::vector<float>> RunOpenAICompatibleEmbedding(const ProviderConfig& provider,\n"
    "                                                             const ModelConfig& model,\n"
    "                                                             const std::vector<std::string>& texts,\n"
    "                                                             const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/) {\n"
    "    // TODO: implement OpenAI-compatible /embeddings batch call\n"
    "    (void)provider; (void)model; (void)texts;\n"
    "    throw std::runtime_error(\"RunOpenAICompatibleEmbedding not yet implemented.\");\n"
    "}\n"
    "\n"
    "static std::unordered_map<std::string, int64_t> g_cooldown_end_sec; // key = \"provider_id|model_id\""
)
if old_after_embed in text6:
    text6 = text6.replace(old_after_embed, new_after_embed)
else:
    print('WARN: after CreateEmbed old not found')

write_utf8('src/openai_client.cpp', text6, enc6)
print('Updated openai_client.cpp (embed stubs)')

print('All modifications applied.')
