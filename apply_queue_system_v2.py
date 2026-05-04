import os

def read_lines(path):
    with open(path, 'rb') as f:
        raw = f.read()
    for enc in ['utf-8-sig', 'utf-8', 'utf-16-le', 'utf-16-be']:
        try:
            text = raw.decode(enc)
            return text.splitlines(True), enc  # keep line endings
        except Exception:
            continue
    raise RuntimeError('Cannot decode ' + path)

def write_lines(path, lines, enc):
    with open(path, 'wb') as f:
        f.write(''.join(lines).encode(enc))

# ---------- 1. types.h: add bypass_queue ----------
lines, enc = read_lines('src/types.h')
for i, line in enumerate(lines):
    if 'bool self_managed_queue = false;' in line:
        indent = line[:len(line) - len(line.lstrip())]
        lines.insert(i + 1, indent + 'bool bypass_queue = false;         // true = binding model skips its own gate and directly acquires target gate(s)\n')
        break
write_lines('src/types.h', lines, enc)
print('Updated types.h')

# ---------- 2. storage.cpp: persist bypass_queue ----------
lines, enc = read_lines('src/storage.cpp')
for i, line in enumerate(lines):
    if '{"self_managed_queue", model.self_managed_queue},' in line:
        lines.insert(i + 1, '        {"bypass_queue", model.bypass_queue},\n')
        break
for i, line in enumerate(lines):
    if 'model.self_managed_queue = item.value("self_managed_queue", false);' in line:
        lines.insert(i + 1, '    model.bypass_queue = item.value("bypass_queue", false);\n')
        break
write_lines('src/storage.cpp', lines, enc)
print('Updated storage.cpp')

# ---------- 3. provider_manager.cpp ----------
lines, enc = read_lines('src/provider_manager.cpp')

# Find kModelEditorSelfManagedCheck
for i, line in enumerate(lines):
    if 'kModelEditorSelfManagedCheck = 2345,' in line:
        lines[i] = line.replace('kModelEditorSelfManagedCheck = 2345,', 'kModelEditorSelfManagedCheck = 2345,\n    kModelEditorBypassCheck = 2346,')
        break

# Find OnCreate: after thinking_check_ creation block insert queue controls
for i, line in enumerate(lines):
    if 'thinking_check_ = CreateButton(L"Thinking capable", kModelEditorThinkingCheck, BS_AUTOCHECKBOX);' in line:
        insert = [
            "        max_active_label_ = CreateLabel(L\"Max active (0=default):\", kModelEditorMaxActiveLabel);\n",
            "        max_active_edit_ = CreateEdit(model_.max_active_requests > 0 ? std::to_wstring(model_.max_active_requests).c_str() : L\"\", kModelEditorMaxActiveEdit, ES_NUMBER);\n",
            "        max_queue_label_ = CreateLabel(L\"Max queue (0=default):\", kModelEditorMaxQueueLabel);\n",
            "        max_queue_edit_ = CreateEdit(model_.max_queue_size > 0 ? std::to_wstring(model_.max_queue_size).c_str() : L\"\", kModelEditorMaxQueueEdit, ES_NUMBER);\n",
            "        self_managed_check_ = CreateButton(L\"Self-managed queue\", kModelEditorSelfManagedCheck, BS_AUTOCHECKBOX);\n",
            "        Button_SetCheck(self_managed_check_, model_.self_managed_queue ? BST_CHECKED : BST_UNCHECKED);\n",
            "        bypass_check_ = CreateButton(L\"Bypass queue (binding direct call)\", kModelEditorBypassCheck, BS_AUTOCHECKBOX);\n",
            "        Button_SetCheck(bypass_check_, model_.bypass_queue ? BST_CHECKED : BST_UNCHECKED);\n",
        ]
        for j, l in enumerate(insert):
            lines.insert(i + 1 + j, l)
        break

# Find OnCreate: update font list (replace one line)
for i, line in enumerate(lines):
    if 'thinking_check_, max_completion_check_, reasoning_label_, reasoning_combo_, verbosity_label_, verbosity_combo_, routing_label_, routing_combo_, targets_label_, targets_list_, add_target_button_, edit_target_button_, remove_target_button_, move_target_up_button_, move_target_down_button_, save_button_, cancel_button_)' in line:
        lines[i] = line.replace(
            'thinking_check_, max_completion_check_',
            'thinking_check_, max_active_label_, max_active_edit_, max_queue_label_, max_queue_edit_, self_managed_check_, bypass_check_, max_completion_check_'
        )
        break

# Find LayoutControls: after thinking check insert queue rows
for i, line in enumerate(lines):
    if 'MoveWindow(thinking_check_, margin + label_width, y, Scale(180), edit_height, TRUE);' in line:
        # find next y += line
        j = i + 1
        while j < len(lines) and 'y += edit_height + gutter;' not in lines[j]:
            j += 1
        insert = [
            "        y += edit_height + gutter;\n",
            "        MoveWindow(max_active_label_, margin, y + Scale(4), label_width, label_height, TRUE);\n",
            "        MoveWindow(max_active_edit_, margin + label_width, y, Scale(90), edit_height, TRUE);\n",
            "        MoveWindow(max_queue_label_, margin + label_width + Scale(100), y + Scale(4), Scale(110), label_height, TRUE);\n",
            "        MoveWindow(max_queue_edit_, margin + label_width + Scale(212), y, Scale(90), edit_height, TRUE);\n",
            "        y += edit_height + gutter;\n",
            "        MoveWindow(self_managed_check_, margin + label_width, y, Scale(180), edit_height, TRUE);\n",
            "        MoveWindow(bypass_check_, margin + label_width + Scale(200), y, Scale(260), edit_height, TRUE);\n",
        ]
        for k, l in enumerate(insert):
            lines.insert(j + 1 + k, l)
        break

# Find ValidateAndSave: after supports_thinking add queue fields
for i, line in enumerate(lines):
    if 'model.supports_thinking = Button_GetCheck(thinking_check_) == BST_CHECKED;' in line:
        insert = [
            "            model.max_active_requests = std::max(0, ParseInt(TrimWide(GetWindowTextString(max_active_edit_))).value_or(0));\n",
            "            model.max_queue_size = std::max(0, ParseInt(TrimWide(GetWindowTextString(max_queue_edit_))).value_or(0));\n",
            "            model.self_managed_queue = Button_GetCheck(self_managed_check_) == BST_CHECKED;\n",
            "            model.bypass_queue = Button_GetCheck(bypass_check_) == BST_CHECKED;\n",
        ]
        for j, l in enumerate(insert):
            lines.insert(i + 1 + j, l)
        break

# Find member declarations: add bypass_check_ after self_managed_check_
for i, line in enumerate(lines):
    if 'HWND self_managed_check_ = nullptr;' in line:
        # there are multiple; we need the one inside ModelEditorDialog (around line 2570)
        # Since it appears 4 times, pick the last one before ProviderParsedUrl 
        pass

# Easier: scan backwards from end for < 200 chars before ProviderParsedUrl
idx = None
for i in range(len(lines) - 1, -1, -1):
    if 'struct ProviderParsedUrl {' in lines[i]:
        # look a few lines above for self_managed_check_
        for j in range(i - 1, max(i - 20, -1), -1):
            if 'HWND self_managed_check_ = nullptr;' in lines[j]:
                idx = j
                break
        break

if idx is not None:
    indent = lines[idx][:len(lines[idx]) - len(lines[idx].lstrip())]
    lines.insert(idx + 1, indent + 'HWND bypass_check_ = nullptr;\n')
else:
    print('WARN: could not find self_managed_check_ member declaration')

write_lines('src/provider_manager.cpp', lines, enc)
print('Updated provider_manager.cpp')

# ---------- 4. openai_client.cpp ----------
lines, enc = read_lines('src/openai_client.cpp')

# Remove the misplaced ResolveBindingTarget block (lines ~30-160)
# Find start: looking for static bool IsBindingProvider inside namespace
start = None
for i, line in enumerate(lines):
    if 'static bool IsBindingProvider(const ProviderConfig& provider) {' in line:
        # verify this is the misplaced one (before IsOllamaLocalProvider)
        start = i - 1  # include "namespace {" line before it
        break

end = None
if start is not None:
    for j in range(start, min(start + 150, len(lines))):
        if 'bool IsOllamaLocalProvider' in lines[j]:
            end = j
            break

if start is not None and end is not None:
    del lines[start:end]
    print('Removed misplaced ResolveBindingTarget')
else:
    print('WARN: Could not locate misplaced block')

# Insert ResolveBindingTarget after GateSlot::~GateSlot()
insert_idx = None
for i, line in enumerate(lines):
    if 'GateSlot::~GateSlot() {' in line:
        # find closing brace and next blank line / namespace
        j = i + 1
        while j < len(lines):
            if lines[j].strip().startswith('}'):
                # check next non-empty line
                k = j + 1
                while k < len(lines) and lines[k].strip() == '':
                    k += 1
                if k < len(lines) and 'namespace {' in lines[k]:
                    insert_idx = k
                else:
                    insert_idx = j + 1
                break
            j += 1
        break

if insert_idx is None:
    print('WARN: Could not find insertion point after GateSlot destructor')
else:
    new_block = [
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
        "            if (ProviderRequestGate::Acquire({candidate->provider_id, candidate->model_id, GateDomain::Chat}, eff_active, eff_queue, on_queue_status)) {\n",
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
        "            if (ProviderRequestGate::Acquire({candidate->provider_id, candidate->model_id, GateDomain::Chat}, eff_active, eff_queue, on_queue_status)) {\n",
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
    for l in reversed(new_block):
        lines.insert(insert_idx, l)
    print('Inserted ResolveBindingTarget after GateSlot destructor')

write_lines('src/openai_client.cpp', lines, enc)
print('Updated openai_client.cpp')

# ---------- 5. openai_client.h ----------
lines, enc = read_lines('src/openai_client.h')
# Add declarations before class OpenAIClient
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
print('Updated openai_client.h')

# ---------- 6. openai_client.cpp – binding bypass in 4 chat methods ----------
lines, enc = read_lines('src/openai_client.cpp')

binding_bypass = [
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

# Function-specific return lines
stream_chat_return = [
    "            return StreamChat(resolved, on_delta, on_queue_status, {});\n",
    "        }\n",
    "        ChatExecutionResult fail;\n",
    "        fail.error = \"Binding resolution failed: no target available for \" + request.model.id;\n",
    "        return fail;\n",
    "    }\n",
]

tool_aware_return = [
    "            return CreateToolAwareCompletion(resolved, tools, on_queue_status, {});\n",
    "        }\n",
    "        ChatCompletionResult fail;\n",
    "        fail.error = \"Binding resolution failed: no target available for \" + request.model.id;\n",
    "        return fail;\n",
    "    }\n",
]

simple_return = [
    "            return CreateSimpleCompletion(resolved, on_queue_status, {});\n",
    "        }\n",
    "        ChatCompletionResult fail;\n",
    "        fail.error = \"Binding resolution failed: no target available for \" + request.model.id;\n",
    "        return fail;\n",
    "    }\n",
]

stream_tool_return = [
    "            return StreamToolAwareCompletion(resolved, tools, on_delta, on_queue_status, {});\n",
    "        }\n",
    "        ChatCompletionResult fail;\n",
    "        fail.error = \"Binding resolution failed: no target available for \" + request.model.id;\n",
    "        return fail;\n",
    "    }\n",
]

def insert_before(lines, search, insertion):
    for i, line in enumerate(lines):
        if search in line:
            for l in reversed(insertion):
                lines.insert(i, l)
            return True
    return False

insert_before(lines, 'ChatExecutionResult OpenAIClient::StreamChat(const ChatRequestOptions& request,', binding_bypass + stream_chat_return)
insert_before(lines, 'ChatCompletionResult OpenAIClient::CreateToolAwareCompletion(const ChatRequestOptions& request,', binding_bypass + tool_aware_return)
insert_before(lines, 'ChatCompletionResult OpenAIClient::CreateSimpleCompletion(const ChatRequestOptions& request,', binding_bypass + simple_return)
insert_before(lines, 'ChatCompletionResult OpenAIClient::StreamToolAwareCompletion(const ChatRequestOptions& request,', binding_bypass + stream_tool_return)

# Also add embedding stubs after CreateEmbedding
for i, line in enumerate(lines):
    if 'return RunOpenAICompatibleEmbedding(provider, model, texts, on_queue_status);' in line:
        j = i
        while j < len(lines) and lines[j].strip() != '}':
            j += 1
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
        for l in reversed(stubs):
            lines.insert(j + 1, l)
        break

write_lines('src/openai_client.cpp', lines, enc)
print('Updated openai_client.cpp')

# ---------- 7. CreateEmbedding rewrite ----------
lines, enc = read_lines('src/openai_client.cpp')
old_embed_start = None
old_embed_end = None
for i, line in enumerate(lines):
    if 'std::vector<std::vector<float>> OpenAIClient::CreateEmbedding(' in line:
        old_embed_start = i
    if old_embed_start is not None and lines[i].strip() == '}' and lines[i+1].strip() == '':
        # count braces to find exact end
        brace_depth = 0
        for j in range(old_embed_start, len(lines)):
            if '{' in lines[j]: brace_depth += lines[j].count('{')
            if '}' in lines[j]: brace_depth -= lines[j].count('}')
            if brace_depth == 0:
                old_embed_end = j
                break
        break

if old_embed_start is None or old_embed_end is None:
    print('WARN: Could not locate old CreateEmbedding body')
else:
    new_embed = [
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
    # remove old block
    del lines[old_embed_start:old_embed_end + 1]
    for l in reversed(new_embed):
        lines.insert(old_embed_start, l)
    print('Rewrote CreateEmbedding')
    write_lines('src/openai_client.cpp', lines, enc)
    print('Updated openai_client.cpp (CreateEmbedding)')

print('All edits complete!')
