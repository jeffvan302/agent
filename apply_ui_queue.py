import os

def read_file(path):
    with open(path, 'rb') as f:
        raw = f.read()
    if raw.startswith(b'\xff\xfe'):
        text = raw.decode('utf-16-le')
        eol = '\r\n' if '\r\n' in text else '\n'
        bom = True
    elif raw.startswith(b'\xfe\xff'):
        text = raw.decode('utf-16-be')
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

# ============== types.h ==============
text, eol, bom = read_file('src/types.h')

old = (
    "    int max_active_requests = 0;       // 0 = use provider default" + eol +
    "    int max_queue_size = 0;            // 0 = use provider default (0 at both levels means default gate = 1 active / 100 queue)" + eol +
    "    bool self_managed_queue = false;   // true = remote worker manages its own queue, local gate bypasses"
)
new = (
    "    int max_active_requests = 0;       // 0 = use provider default" + eol +
    "    int max_queue_size = 0;            // 0 = use provider default (0 at both levels means default gate = 1 active / 100 queue)" + eol +
    "    bool self_managed_queue = false;   // true = remote worker manages its own queue, local gate bypasses" + eol +
    "    bool bypass_queue = false;         // true = binding model skips its own gate and directly acquires target gate(s)"
)
if old in text:
    text = text.replace(old, new)
    print('Updated types.h')
else:
    print('WARN: types.h old not found')
write_file('src/types.h', text, eol, bom)

# ============== storage.cpp ==============
text, eol, bom = read_file('src/storage.cpp')

old1 = (
    '        {"self_managed_queue", model.self_managed_queue},'
)
new1 = (
    '        {"self_managed_queue", model.self_managed_queue},' + eol +
    '        {"bypass_queue", model.bypass_queue},'
)
if old1 in text:
    text = text.replace(old1, new1)
    print('Updated storage.cpp serialize')
else:
    print('WARN: storage.cpp serialize old not found')

old2 = (
    '    model.self_managed_queue = item.value("self_managed_queue", false);'
)
new2 = (
    '    model.self_managed_queue = item.value("self_managed_queue", false);' + eol +
    '    model.bypass_queue = item.value("bypass_queue", false);'
)
if old2 in text:
    text = text.replace(old2, new2)
    print('Updated storage.cpp deserialize')
else:
    print('WARN: storage.cpp deserialize old not found')
write_file('src/storage.cpp', text, eol, bom)

# ============== provider_manager.cpp ==============
text, eol, bom = read_file('src/provider_manager.cpp')

# kModelEditorSelfManagedCheck
old = (
    '    kModelEditorSelfManagedCheck = 2345,' + eol +
    '    kModelEditorSave = IDOK,'
)
new = (
    '    kModelEditorSelfManagedCheck = 2345,' + eol +
    '    kModelEditorBypassCheck = 2346,' + eol +
    '    kModelEditorSave = IDOK,'
)
if old in text:
    text = text.replace(old, new)
    print('Updated control IDs')
else:
    print('WARN: control IDs old not found')

# OnCreate insert queue controls after thinking_check_
old = (
    "        thinking_check_ = CreateButton(L\"Thinking capable\", kModelEditorThinkingCheck, BS_AUTOCHECKBOX);" + eol +
    "        max_completion_check_ = CreateButton(L\"Prefer max_completion_tokens\", kModelEditorMaxCompletionCheck, BS_AUTOCHECKBOX);"
)
new = (
    "        thinking_check_ = CreateButton(L\"Thinking capable\", kModelEditorThinkingCheck, BS_AUTOCHECKBOX);" + eol +
    "        max_active_label_ = CreateLabel(L\"Max active (0=default):\", kModelEditorMaxActiveLabel);" + eol +
    "        max_active_edit_ = CreateEdit(model_.max_active_requests > 0 ? std::to_wstring(model_.max_active_requests).c_str() : L\"\", kModelEditorMaxActiveEdit, ES_NUMBER);" + eol +
    "        max_queue_label_ = CreateLabel(L\"Max queue (0=default):\", kModelEditorMaxQueueLabel);" + eol +
    "        max_queue_edit_ = CreateEdit(model_.max_queue_size > 0 ? std::to_wstring(model_.max_queue_size).c_str() : L\"\", kModelEditorMaxQueueEdit, ES_NUMBER);" + eol +
    "        self_managed_check_ = CreateButton(L\"Self-managed queue\", kModelEditorSelfManagedCheck, BS_AUTOCHECKBOX);" + eol +
    "        Button_SetCheck(self_managed_check_, model_.self_managed_queue ? BST_CHECKED : BST_UNCHECKED);" + eol +
    "        bypass_check_ = CreateButton(L\"Bypass queue (binding direct call)\", kModelEditorBypassCheck, BS_AUTOCHECKBOX);" + eol +
    "        Button_SetCheck(bypass_check_, model_.bypass_queue ? BST_CHECKED : BST_UNCHECKED);" + eol +
    "        max_completion_check_ = CreateButton(L\"Prefer max_completion_tokens\", kModelEditorMaxCompletionCheck, BS_AUTOCHECKBOX);"
)
if old in text:
    text = text.replace(old, new)
    print('Updated OnCreate')
else:
    print('WARN: OnCreate old not found')

# Font list update
old = (
    '            for (HWND control : {provider_label_, provider_combo_, id_label_, id_edit_, ollama_load_info_button_, ollama_search_button_, display_label_, display_edit_, context_label_, context_edit_, ollama_timeout_label_, ollama_timeout_edit_, ollama_num_threads_label_, ollama_num_threads_edit_, ollama_no_gpu_check_, ollama_gpu_layers_label_, ollama_gpu_layers_edit_, ollama_ctx_len_label_, ollama_ctx_len_edit_, ollama_verbose_check_, streaming_check_, tools_check_, vision_check_, embedding_check_, thinking_check_, max_completion_check_, reasoning_label_, reasoning_combo_, verbosity_label_, verbosity_combo_, routing_label_, routing_combo_, targets_label_, targets_list_, add_target_button_, edit_target_button_, remove_target_button_, move_target_up_button_, move_target_down_button_, save_button_, cancel_button_}) {'
)
new = (
    '            for (HWND control : {provider_label_, provider_combo_, id_label_, id_edit_, ollama_load_info_button_, ollama_search_button_, display_label_, display_edit_, context_label_, context_edit_, ollama_timeout_label_, ollama_timeout_edit_, ollama_num_threads_label_, ollama_num_threads_edit_, ollama_no_gpu_check_, ollama_gpu_layers_label_, ollama_gpu_layers_edit_, ollama_ctx_len_label_, ollama_ctx_len_edit_, ollama_verbose_check_, streaming_check_, tools_check_, vision_check_, embedding_check_, thinking_check_, max_active_label_, max_active_edit_, max_queue_label_, max_queue_edit_, self_managed_check_, bypass_check_, max_completion_check_, reasoning_label_, reasoning_combo_, verbosity_label_, verbosity_combo_, routing_label_, routing_combo_, targets_label_, targets_list_, add_target_button_, edit_target_button_, remove_target_button_, move_target_up_button_, move_target_down_button_, save_button_, cancel_button_}) {'
)
if old in text:
    text = text.replace(old, new)
    print('Updated font list')
else:
    print('WARN: font list old not found')

# Layout update after thinking check
old = (
    '        MoveWindow(thinking_check_, margin + label_width, y, Scale(180), edit_height, TRUE);' + eol +
    '        y += edit_height + gutter;' + eol +
    '        MoveWindow(max_completion_check_, margin + label_width, y, Scale(220), edit_height, TRUE);'
)
new = (
    '        MoveWindow(thinking_check_, margin + label_width, y, Scale(180), edit_height, TRUE);' + eol +
    '        y += edit_height + gutter;' + eol +
    '        MoveWindow(max_active_label_, margin, y + Scale(4), label_width, label_height, TRUE);' + eol +
    '        MoveWindow(max_active_edit_, margin + label_width, y, Scale(90), edit_height, TRUE);' + eol +
    '        MoveWindow(max_queue_label_, margin + label_width + Scale(100), y + Scale(4), Scale(110), label_height, TRUE);' + eol +
    '        MoveWindow(max_queue_edit_, margin + label_width + Scale(212), y, Scale(90), edit_height, TRUE);' + eol +
    '        y += edit_height + gutter;' + eol +
    '        MoveWindow(self_managed_check_, margin + label_width, y, Scale(180), edit_height, TRUE);' + eol +
    '        MoveWindow(bypass_check_, margin + label_width + Scale(200), y, Scale(260), edit_height, TRUE);' + eol +
    '        y += edit_height + gutter;' + eol +
    '        MoveWindow(max_completion_check_, margin + label_width, y, Scale(220), edit_height, TRUE);'
)
if old in text:
    text = text.replace(old, new)
    print('Updated Layout')
else:
    print('WARN: Layout old not found')

# ValidateAndSave update
old = (
    '            model.supports_thinking = Button_GetCheck(thinking_check_) == BST_CHECKED;' + eol +
    '            model.prefer_max_completion_tokens = Button_GetCheck(max_completion_check_) == BST_CHECKED;'
)
new = (
    '            model.supports_thinking = Button_GetCheck(thinking_check_) == BST_CHECKED;' + eol +
    '            model.max_active_requests = std::max(0, ParseInt(TrimWide(GetWindowTextString(max_active_edit_))).value_or(0));' + eol +
    '            model.max_queue_size = std::max(0, ParseInt(TrimWide(GetWindowTextString(max_queue_edit_))).value_or(0));' + eol +
    '            model.self_managed_queue = Button_GetCheck(self_managed_check_) == BST_CHECKED;' + eol +
    '            model.bypass_queue = Button_GetCheck(bypass_check_) == BST_CHECKED;' + eol +
    '            model.prefer_max_completion_tokens = Button_GetCheck(max_completion_check_) == BST_CHECKED;'
)
if old in text:
    text = text.replace(old, new)
    print('Updated ValidateAndSave')
else:
    print('WARN: ValidateAndSave old not found')

# Add bypass_check_ member after self_managed_check_
old = (
    '    HWND max_active_label_ = nullptr;' + eol +
    '    HWND max_active_edit_ = nullptr;' + eol +
    '    HWND max_queue_label_ = nullptr;' + eol +
    '    HWND max_queue_edit_ = nullptr;' + eol +
    '    HWND self_managed_check_ = nullptr;' + eol +
    '};' + eol + eol +
    'struct ProviderParsedUrl {'
)
new = (
    '    HWND max_active_label_ = nullptr;' + eol +
    '    HWND max_active_edit_ = nullptr;' + eol +
    '    HWND max_queue_label_ = nullptr;' + eol +
    '    HWND max_queue_edit_ = nullptr;' + eol +
    '    HWND self_managed_check_ = nullptr;' + eol +
    '    HWND bypass_check_ = nullptr;' + eol +
    '};' + eol + eol +
    'struct ProviderParsedUrl {'
)
if old in text:
    text = text.replace(old, new)
    print('Updated members')
else:
    print('WARN: members old not found')

write_file('src/provider_manager.cpp', text, eol, bom)
print('Done provider_manager.cpp')
print('All edits complete.')
