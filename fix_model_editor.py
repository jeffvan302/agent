import codecs

input_path = 'src/provider_manager.cpp'

with open(input_path, 'rb') as f:
    raw = f.read()
    content = raw.decode('utf-16-le')

# Use exact strings found in the file
old = ('        max_completion_check_ = CreateButton(L"Prefer max_completion_tokens", kModelEditorMaxCompletionCheck, BS_AUTOCHECKBOX);\r\n'
       '        reasoning_label_ = CreateLabel(L"Reasoning effort:", kModelEditorReasoningLabel);')
new = ('        max_completion_check_ = CreateButton(L"Prefer max_completion_tokens", kModelEditorMaxCompletionCheck, BS_AUTOCHECKBOX);\r\n'
       '        max_active_label_ = CreateLabel(L"Max active requests (0=no limit):", kModelEditorMaxActiveLabel);\r\n'
       '        max_active_edit_ = CreateEdit(std::to_wstring(std::max(0, model_.max_active_requests)).c_str(), kModelEditorMaxActiveEdit, ES_NUMBER);\r\n'
       '        max_queue_label_ = CreateLabel(L"Max queue size (0=no limit):", kModelEditorMaxQueueLabel);\r\n'
       '        max_queue_edit_ = CreateEdit(std::to_wstring(std::max(0, model_.max_queue_size)).c_str(), kModelEditorMaxQueueEdit, ES_NUMBER);\r\n'
       '        self_managed_check_ = CreateButton(L"Self-managed queue", kModelEditorSelfManagedCheck, BS_AUTOCHECKBOX);\r\n'
       '        Button_SetCheck(self_managed_check_, model_.self_managed_queue ? BST_CHECKED : BST_UNCHECKED);\r\n'
       '        reasoning_label_ = CreateLabel(L"Reasoning effort:", kModelEditorReasoningLabel);')

if old in content:
    print('Found old string, replacing')
    content = content.replace(old, new, 1)
else:
    print('Old string NOT FOUND - searching for partial')
    # Try without the exact whitespace
    idx = content.find('max_completion_check_ = CreateButton(L"Prefer max_completion_tokens"')
    if idx != -1:
        print('Found at index', idx)
        end = content.find('reasoning_label_ = CreateLabel', idx)
        print('End at index', end)
    else:
        print('Partial not found either')

with open(input_path, 'wb') as f:
    f.write(content.encode('utf-16-le'))

print('Done. File size:', len(content.encode('utf-16-le')))