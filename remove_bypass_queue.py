import os

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

# ===================== 1. types.h =====================
text, eol, bom = read_file('src/types.h')
old = (
    "    bool self_managed_queue = false;   // true = remote worker manages its own queue, local gate bypasses" + eol +
    "    bool bypass_queue = false;         // true = binding model skips its own gate and directly acquires target gate(s)"
)
new = (
    "    bool self_managed_queue = false;   // true = remote worker manages its own queue, local gate bypasses"
)
if old in text:
    text = text.replace(old, new)
    print('Removed bypass_queue from types.h')
else:
    print('WARN: types.h old not found')
write_file('src/types.h', text, eol, bom)

# ===================== 2. storage.cpp =====================
text, eol, bom = read_file('src/storage.cpp')
# Serialize
old1 = (
    '        {"self_managed_queue", model.self_managed_queue},' + eol +
    '        {"bypass_queue", model.bypass_queue},'
)
new1 = (
    '        {"self_managed_queue", model.self_managed_queue},'
)
if old1 in text:
    text = text.replace(old1, new1)
    print('Removed bypass_queue from storage serialize')
else:
    print('WARN: storage serialize old not found')

# Deserialize
old2 = (
    '    model.self_managed_queue = item.value("self_managed_queue", false);' + eol +
    '    model.bypass_queue = item.value("bypass_queue", false);'
)
new2 = (
    '    model.self_managed_queue = item.value("self_managed_queue", false);'
)
if old2 in text:
    text = text.replace(old2, new2)
    print('Removed bypass_queue from storage deserialize')
else:
    print('WARN: storage deserialize old not found')
write_file('src/storage.cpp', text, eol, bom)

# ===================== 3. provider_manager.cpp =====================
text, eol, bom = read_file('src/provider_manager.cpp')

# Remove bypass checkbox from OnCreate
old = (
    "        self_managed_check_ = CreateButton(L\"Self-managed queue\", kModelEditorSelfManagedCheck, BS_AUTOCHECKBOX);" + eol +
    "        Button_SetCheck(self_managed_check_, model_.self_managed_queue ? BST_CHECKED : BST_UNCHECKED);" + eol +
    "        bypass_check_ = CreateButton(L\"Bypass queue (binding direct call)\", kModelEditorBypassCheck, BS_AUTOCHECKBOX);" + eol +
    "        Button_SetCheck(bypass_check_, model_.bypass_queue ? BST_CHECKED : BST_UNCHECKED);" + eol +
    "        max_completion_check_ = CreateButton(L\"Prefer max_completion_tokens\", kModelEditorMaxCompletionCheck, BS_AUTOCHECKBOX);"
)
new = (
    "        self_managed_check_ = CreateButton(L\"Self-managed queue\", kModelEditorSelfManagedCheck, BS_AUTOCHECKBOX);" + eol +
    "        Button_SetCheck(self_managed_check_, model_.self_managed_queue ? BST_CHECKED : BST_UNCHECKED);" + eol +
    "        max_completion_check_ = CreateButton(L\"Prefer max_completion_tokens\", kModelEditorMaxCompletionCheck, BS_AUTOCHECKBOX);"
)
if old in text:
    text = text.replace(old, new)
    print('Removed bypass checkbox from OnCreate')
else:
    print('WARN: OnCreate bypass checkbox old not found')

# Remove bypass from LayoutControls
old = (
    "        MoveWindow(self_managed_check_, margin + label_width, y, Scale(180), edit_height, TRUE);" + eol +
    "        MoveWindow(bypass_check_, margin + label_width + Scale(200), y, Scale(260), edit_height, TRUE);" + eol +
    "        y += edit_height + gutter;" + eol +
    "        MoveWindow(max_completion_check_, margin + label_width, y, Scale(220), edit_height, TRUE);"
)
new = (
    "        MoveWindow(self_managed_check_, margin + label_width, y, Scale(180), edit_height, TRUE);" + eol +
    "        y += edit_height + gutter;" + eol +
    "        MoveWindow(max_completion_check_, margin + label_width, y, Scale(220), edit_height, TRUE);"
)
if old in text:
    text = text.replace(old, new)
    print('Removed bypass from LayoutControls')
else:
    print('WARN: Layout bypass old not found')

# Remove bypass from ValidateAndSave
old = (
    "            model.self_managed_queue = Button_GetCheck(self_managed_check_) == BST_CHECKED;" + eol +
    "            model.bypass_queue = Button_GetCheck(bypass_check_) == BST_CHECKED;" + eol +
    "            model.prefer_max_completion_tokens = Button_GetCheck(max_completion_check_) == BST_CHECKED;"
)
new = (
    "            model.self_managed_queue = Button_GetCheck(self_managed_check_) == BST_CHECKED;" + eol +
    "            model.prefer_max_completion_tokens = Button_GetCheck(max_completion_check_) == BST_CHECKED;"
)
if old in text:
    text = text.replace(old, new)
    print('Removed bypass from ValidateAndSave')
else:
    print('WARN: ValidateAndSave bypass old not found')

# Remove bypass_check_ from members
old = (
    '    HWND self_managed_check_ = nullptr;' + eol +
    '    HWND bypass_check_ = nullptr;' + eol +
    '};' + eol + eol +
    'struct ProviderParsedUrl {'
)
new = (
    '    HWND self_managed_check_ = nullptr;' + eol +
    '};' + eol + eol +
    'struct ProviderParsedUrl {'
)
if old in text:
    text = text.replace(old, new)
    print('Removed bypass_check_ from members')
else:
    print('WARN: Members bypass old not found')

# Remove bypass from font list (multiple occurrences)
old_font = (
    '            for (HWND control : {provider_label_, provider_combo_, id_label_, id_edit_, ollama_load_info_button_, ollama_search_button_, display_label_, display_edit_, context_label_, context_edit_, ollama_timeout_label_, ollama_timeout_edit_, ollama_num_threads_label_, ollama_num_threads_edit_, ollama_no_gpu_check_, ollama_gpu_layers_label_, ollama_gpu_layers_edit_, ollama_ctx_len_label_, ollama_ctx_len_edit_, ollama_verbose_check_, streaming_check_, tools_check_, vision_check_, embedding_check_, thinking_check_, max_active_label_, max_active_edit_, max_queue_label_, max_queue_edit_, self_managed_check_, bypass_check_, max_completion_check_, reasoning_label_, reasoning_combo_, verbosity_label_, verbosity_combo_, routing_label_, routing_combo_, targets_label_, targets_list_, add_target_button_, edit_target_button_, remove_target_button_, move_target_up_button_, move_target_down_button_, save_button_, cancel_button_}) {'
)
new_font = (
    '            for (HWND control : {provider_label_, provider_combo_, id_label_, id_edit_, ollama_load_info_button_, ollama_search_button_, display_label_, display_edit_, context_label_, context_edit_, ollama_timeout_label_, ollama_timeout_edit_, ollama_num_threads_label_, ollama_num_threads_edit_, ollama_no_gpu_check_, ollama_gpu_layers_label_, ollama_gpu_layers_edit_, ollama_ctx_len_label_, ollama_ctx_len_edit_, ollama_verbose_check_, streaming_check_, tools_check_, vision_check_, embedding_check_, thinking_check_, max_active_label_, max_active_edit_, max_queue_label_, max_queue_edit_, self_managed_check_, max_completion_check_, reasoning_label_, reasoning_combo_, verbosity_label_, verbosity_combo_, routing_label_, routing_combo_, targets_label_, targets_list_, add_target_button_, edit_target_button_, remove_target_button_, move_target_up_button_, move_target_down_button_, save_button_, cancel_button_}) {'
)
if old_font in text:
    text = text.replace(old_font, new_font)
    print('Updated font list (binding model editor)')

# Also in the other ModelEditorDialog (if there's another one)
# Actually there are multiple classes with similar member names. The script only handles the one with bypass_check_ in it.
write_file('src/provider_manager.cpp', text, eol, bom)
print('Done provider_manager.cpp')

# ===================== 4. openai_client.cpp =====================
text, eol, bom = read_file('src/openai_client.cpp')

# Replace all binding bypass checks: remove bypass_queue, keep is_binding_model + binding_depth
# Pattern: "if (request.model.is_binding_model && request.model.bypass_queue && request.binding_depth < 8) {"
text = text.replace(
    'if (request.model.is_binding_model && request.model.bypass_queue && request.binding_depth < 8) {',
    'if (request.model.is_binding_model && request.binding_depth < 8) {'
)
print('Replaced chat binding checks')

# Also in CreateEmbedding: "if (model.is_binding_model && model.bypass_queue) {"
text = text.replace(
    'if (model.is_binding_model && model.bypass_queue) {',
    'if (model.is_binding_model) {'
)
print('Replaced embedding binding checks')

# Remove bypass_queue references from GateSlot::Acquire model lookup (if any)
# (bypass_queue was not in GateSlot::Acquire in the original code, only in chat methods)

# Add ComputeBindingModelCapacity helper function after ResolveBindingTarget
old_helper = (
    "    return false;" + eol +
    "}" + eol + eol +
    "namespace {" + eol +
    "struct InternetHandleCloser {"
)
new_helper = (
    "    return false;" + eol +
    "}" + eol + eol +
    "// Compute total parallel capacity for a binding model (sum of target max_active_requests)." + eol +
    "// Self-managed targets contribute 0 to the local count because their queue is remote." + eol +
    "int ComputeBindingModelCapacity(const ModelConfig& binding_model," + eol +
    "                                const std::vector<ProviderConfig>& providers) {" + eol +
    "    if (!binding_model.is_binding_model || binding_model.binding_targets.empty()) return 0;" + eol +
    "    int total = 0;" + eol +
    "    for (const auto& t : binding_model.binding_targets) {" + eol +
    "        int target_active = 0;" + eol +
    "        bool target_self_managed = false;" + eol +
    "        for (const auto& p : providers) {" + eol +
    "            if (p.id != t.provider_id) continue;" + eol +
    "            for (const auto& m : p.models) {" + eol +
    "                if (m.id == t.model_id) {" + eol +
    "                    target_active = std::max(0, m.max_active_requests);" + eol +
    "                    target_self_managed = m.self_managed_queue;" + eol +
    "                    break;" + eol +
    "                }" + eol +
    "            }" + eol +
    "            break;" + eol +
    "        }" + eol +
    "        // Self-managed targets do not consume local capacity" + eol +
    "        if (!target_self_managed) total += target_active > 0 ? target_active : 1;" + eol +
    "    }" + eol +
    "    return total;" + eol +
    "}" + eol + eol +
    "namespace {" + eol +
    "struct InternetHandleCloser {"
)
if old_helper in text:
    text = text.replace(old_helper, new_helper)
    print('Added ComputeBindingModelCapacity')
else:
    print('WARN: Could not insert ComputeBindingModelCapacity')

write_file('src/openai_client.cpp', text, eol, bom)
print('Done openai_client.cpp')

# ===================== 5. openai_client.h =====================
text, eol, bom = read_file('src/openai_client.h')

# Remove binding target / routing declarations that we added earlier if they reference bypass
# Actually we should keep ResolveBindingTarget and the embedding stubs, just no bypass_queue references
# Check if bypass_queue is mentioned anywhere
if 'bypass_queue' in text:
    print('WARN: bypass_queue still in openai_client.h')
else:
    print('openai_client.h clean')

# Add ComputeBindingModelCapacity declaration
old_decl = (
    'bool ResolveBindingTarget(const std::vector<ProviderConfig>& providers,' + eol +
    '                          const ModelConfig& binding_model,' + eol +
    '                          const ChatRequestOptions& original_request,' + eol +
    '                          ChatRequestOptions* out_request,' + eol +
    '                          const std::function<void(const ProviderQueueStatus&)>& on_queue_status);' + eol + eol +
    'std::vector<std::vector<float>> RunOllamaLocalEmbedding'
)
new_decl = (
    'bool ResolveBindingTarget(const std::vector<ProviderConfig>& providers,' + eol +
    '                          const ModelConfig& binding_model,' + eol +
    '                          const ChatRequestOptions& original_request,' + eol +
    '                          ChatRequestOptions* out_request,' + eol +
    '                          const std::function<void(const ProviderQueueStatus&)>& on_queue_status);' + eol + eol +
    'int ComputeBindingModelCapacity(const ModelConfig& binding_model,' + eol +
    '                                const std::vector<ProviderConfig>& providers);' + eol + eol +
    'std::vector<std::vector<float>> RunOllamaLocalEmbedding'
)
if old_decl in text:
    text = text.replace(old_decl, new_decl)
    print('Added ComputeBindingModelCapacity declaration')
else:
    print('WARN: Could not add ComputeBindingModelCapacity declaration')
write_file('src/openai_client.h', text, eol, bom)

print('All edits complete.')
