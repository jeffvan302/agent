import codecs

input_path = 'src/provider_manager.cpp'
output_path = 'src/provider_manager.cpp'

with open(input_path, 'rb') as f:
    raw = f.read()
    content = raw.decode('utf-16-le')

# Fix the OnCreate edit for ModelEditor - use unique string
old_oncreate = ('        max_completion_check_ = CreateButton(L"Prefer max_completion_tokens", kModelEditorMaxCompletionCheck, BS_AUTOCHECKBOX);\n'
                '        Button_SetCheck(max_completion_check_, model_.prefer_max_completion_tokens ? BST_CHECKED : BST_UNCHECKED);\n'
                '        reasoning_label_ = CreateLabel(L"Reasoning effort:", kModelEditorReasoningLabel);')
new_oncreate = ('        max_completion_check_ = CreateButton(L"Prefer max_completion_tokens", kModelEditorMaxCompletionCheck, BS_AUTOCHECKBOX);\n'
                '        Button_SetCheck(max_completion_check_, model_.prefer_max_completion_tokens ? BST_CHECKED : BST_UNCHECKED);\n'
                '        max_active_label_ = CreateLabel(L"Max active requests (0=no limit):", kModelEditorMaxActiveLabel);\n'
                '        max_active_edit_ = CreateEdit(std::to_wstring(std::max(0, model_.max_active_requests)).c_str(), kModelEditorMaxActiveEdit, ES_NUMBER);\n'
                '        max_queue_label_ = CreateLabel(L"Max queue size (0=no limit):", kModelEditorMaxQueueLabel);\n'
                '        max_queue_edit_ = CreateEdit(std::to_wstring(std::max(0, model_.max_queue_size)).c_str(), kModelEditorMaxQueueEdit, ES_NUMBER);\n'
                '        self_managed_check_ = CreateButton(L"Self-managed queue", kModelEditorSelfManagedCheck, BS_AUTOCHECKBOX);\n'
                '        Button_SetCheck(self_managed_check_, model_.self_managed_queue ? BST_CHECKED : BST_UNCHECKED);\n'
                '        reasoning_label_ = CreateLabel(L"Reasoning effort:", kModelEditorReasoningLabel);')
content = content.replace(old_oncreate, new_oncreate)

# Fix the ValidateAndSave edit
old_validate = '        model.supports_thinking = Button_GetCheck(thinking_check_) == BST_CHECKED;' + content[content.find('model.supports_thinking = Button_GetCheck(thinking_check_) == BST_CHECKED;') + len('        model.supports_thinking = Button_GetCheck(thinking_check_) == BST_CHECKED;'):]
# Instead, search for a more unique context
old_validate = '        model.supports_thinking = Button_GetCheck(thinking_check_) == BST_CHECKED;\n        return true;'
new_validate = ('        model.supports_thinking = Button_GetCheck(thinking_check_) == BST_CHECKED;\n'
                '        model.max_active_requests = std::max(0, ParseInt(GetWindowTextString(max_active_edit_)).value_or(0));\n'
                '        model.max_queue_size = std::max(0, ParseInt(GetWindowTextString(max_queue_edit_)).value_or(0));\n'
                '        model.self_managed_queue = Button_GetCheck(self_managed_check_) == BST_CHECKED;\n'
                '        return true;')
content = content.replace(old_validate, new_validate)

# Fix the font loop
# The ModelEditor font loop is the SECOND occurrence of the pattern
# Find first occurrence (ProviderEditor), skip it, then replace second
old_font1 = '                move_target_up_button_, move_target_down_button_, save_button_, cancel_button_})'
new_font1 = '                move_target_up_button_, move_target_down_button_, save_button_, cancel_button_, max_active_label_, max_active_edit_, max_queue_label_, max_queue_edit_, self_managed_check_})'

idx = content.find(old_font1)
if idx != -1:
    # First hit is ProviderEditor - skip and look for second
    idx2 = content.find(old_font1, idx + 1)
    if idx2 != -1:
        # ModelEditor font loop is at idx2
        content = content[:idx2] + new_font1 + content[idx2 + len(old_font1):]

with open(output_path, 'wb') as f:
    f.write(content.encode('utf-16-le'))

print('Done. Written', len(content.encode('utf-16-le')), 'bytes')