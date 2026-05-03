import codecs, re, sys

with open('src/provider_manager.cpp', 'rb') as f:
    content = f.read()
# decode utf-16-le with bom
if content.startswith(codecs.BOM_UTF16_LE) or content.startswith(codecs.BOM_UTF16):
    text = content.decode('utf-16-le')
else:
    text = content.decode('utf-16-le')

# helper for CRLF
n = '\r\n'

# 1. enum ControlId insert kTestQueue
old1 = f'    kTestConnection = 2009,{n}    kStatusLabel = 2010,'
new1 = f'    kTestConnection = 2009,{n}    kTestQueue = 2010,{n}    kStatusLabel = 2011,'
assert old1 in text, 'enum not found'
text = text.replace(old1, new1)

# 2. OnCreate test button
old2 = f'        test_connection_button_ = CreateWindowExW(0, L"BUTTON", L"Test Connection", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kTestConnection), nullptr, nullptr);{n}        close_button_ = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kCloseButton), nullptr, nullptr);'
new2 = f'        test_connection_button_ = CreateWindowExW(0, L"BUTTON", L"Test Connection", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kTestConnection), nullptr, nullptr);{n}        test_queue_button_ = CreateWindowExW(0, L"BUTTON", L"Test Queue", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kTestQueue), nullptr, nullptr);{n}        close_button_ = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kCloseButton), nullptr, nullptr);'
assert old2 in text, 'OnCreate buttons not found'
text = text.replace(old2, new2)

# 3. OnCreate font loop
m3 = re.search(r'(for \(HWND child : \{' + r'[^}]+test_connection_button_,)\r\n\s*(close_button_\}\))', text)
if m3:
    text = text.replace(m3.group(0), m3.group(1) + ' test_queue_button_, ' + m3.group(2))
else:
    raise AssertionError('font loop not found')

# 4. LayoutControls footer positions
old4 = f'        SetWindowPos(test_connection_button_, nullptr, width - margin - 210, y, 100, button_height, SWP_NOZORDER);{n}        SetWindowPos(close_button_, nullptr, width - margin - 90, y, 80, button_height, SWP_NOZORDER);'
new4 = f'        SetWindowPos(test_connection_button_, nullptr, width - margin - 310, y, 100, button_height, SWP_NOZORDER);{n}        SetWindowPos(test_queue_button_, nullptr, width - margin - 200, y, 100, button_height, SWP_NOZORDER);{n}        SetWindowPos(close_button_, nullptr, width - margin - 90, y, 80, button_height, SWP_NOZORDER);'
assert old4 in text, 'LayoutControls not found'
text = text.replace(old4, new4)

# 5. OnCommand switch
old5 = f'        case kTestConnection:{n}            TestConnection();{n}            break;{n}        case kStatusLabel:'
new5 = f'        case kTestConnection:{n}            TestConnection();{n}            break;{n}        case kTestQueue:{n}            TestQueue();{n}            break;{n}        case kStatusLabel:'
assert old5 in text, 'OnCommand not found'
text = text.replace(old5, new5)

# 6. ModelEditor enum
old6 = f'    kModelEditorOllamaVerboseCheck = 2339,{n}    kModelEditorOllamaPull = 2340,'
new6 = f'    kModelEditorOllamaVerboseCheck = 2339,{n}    kModelEditorMaxActiveLabel = 2341,{n}    kModelEditorMaxActiveEdit = 2342,{n}    kModelEditorMaxQueueLabel = 2343,{n}    kModelEditorMaxQueueEdit = 2344,{n}    kModelEditorSelfManagedCheck = 2345,{n}    kModelEditorOllamaPull = 2346,'
assert old6 in text, 'ModelEditor enum not found'
text = text.replace(old6, new6)

# 7. ModelEditor OnCreate after max_completion_check_
old7 = f'        max_completion_check_ = CreateButton(L"Prefer max_completion_tokens", kModelEditorMaxCompletionCheck, BS_AUTOCHECKBOX);{n}        Button_SetCheck(max_completion_check_, model_.prefer_max_completion_tokens ? BST_CHECKED : BST_UNCHECKED);{n}        reasoning_label_ = CreateLabel(L"Reasoning effort:", kModelEditorReasoningLabel);'
new7 = f'        max_completion_check_ = CreateButton(L"Prefer max_completion_tokens", kModelEditorMaxCompletionCheck, BS_AUTOCHECKBOX);{n}        Button_SetCheck(max_completion_check_, model_.prefer_max_completion_tokens ? BST_CHECKED : BST_UNCHECKED);{n}        max_active_label_ = CreateLabel(L"Max active requests (0=no limit):", kModelEditorMaxActiveLabel);{n}        max_active_edit_ = CreateEdit(std::to_wstring(std::max(0, model_.max_active_requests)).c_str(), kModelEditorMaxActiveEdit, ES_NUMBER);{n}        max_queue_label_ = CreateLabel(L"Max queue size (0=no limit):", kModelEditorMaxQueueLabel);{n}        max_queue_edit_ = CreateEdit(std::to_wstring(std::max(0, model_.max_queue_size)).c_str(), kModelEditorMaxQueueEdit, ES_NUMBER);{n}        self_managed_check_ = CreateButton(L"Self-managed queue", kModelEditorSelfManagedCheck, BS_AUTOCHECKBOX);{n}        Button_SetCheck(self_managed_check_, model_.self_managed_queue ? BST_CHECKED : BST_UNCHECKED);{n}        reasoning_label_ = CreateLabel(L"Reasoning effort:", kModelEditorReasoningLabel);'
assert old7 in text, 'ModelEditor OnCreate not found'
text = text.replace(old7, new7)

# 8. font loop ModelEditor
m8 = re.search(r'(for \(HWND child : \{' + r'[^}]+max_completion_check_,)\r\n\s*(reasoning_label_\}\))', text)
if m8:
    text = text.replace(m8.group(0), m8.group(1) + ' max_active_label_, max_active_edit_, max_queue_label_, max_queue_edit_, self_managed_check_, ' + m8.group(2))
else:
    raise AssertionError('ModelEditor font loop not found')

# 9. LayoutControls ModelEditor
old9 = f'        y += edit_height + gutter;{n}{n}        // Reasoning effort row{n}        SetWindowPos(reasoning_label_, nullptr, margin, y, label_width, edit_height, SWP_NOZORDER);'
new9 = f'        y += edit_height + gutter;{n}{n}        // Max active requests row{n}        SetWindowPos(max_active_label_, nullptr, margin, y, label_width, edit_height, SWP_NOZORDER);{n}        SetWindowPos(max_active_edit_, nullptr, margin + label_width, y, small_field_width, edit_height, SWP_NOZORDER);{n}{n}        y += edit_height + gutter;{n}{n}        // Max queue size row{n}        SetWindowPos(max_queue_label_, nullptr, margin, y, label_width, edit_height, SWP_NOZORDER);{n}        SetWindowPos(max_queue_edit_, nullptr, margin + label_width, y, small_field_width, edit_height, SWP_NOZORDER);{n}{n}        y += edit_height + gutter;{n}{n}        // Self-managed queue row{n}        SetWindowPos(self_managed_check_, nullptr, margin, y, label_width + small_field_width, edit_height, SWP_NOZORDER);{n}{n}        y += edit_height + gutter;{n}{n}        // Reasoning effort row{n}        SetWindowPos(reasoning_label_, nullptr, margin, y, label_width, edit_height, SWP_NOZORDER);'
assert old9 in text, 'ModelEditor layout not found'
text = text.replace(old9, new9)

# 10. ValidateAndSave
old10 = f'        model.prefer_max_completion_tokens = Button_GetCheck(max_completion_check_) == BST_CHECKED;{n}        auto parse_non_negative = [&](HWND edit, const wchar_t* label) -> std::optional<int> {'
new10 = f'        model.prefer_max_completion_tokens = Button_GetCheck(max_completion_check_) == BST_CHECKED;{n}        model.max_active_requests = std::max(0, ParseInt(GetWindowTextString(max_active_edit_)).value_or(0));{n}        model.max_queue_size = std::max(0, ParseInt(GetWindowTextString(max_queue_edit_)).value_or(0));{n}        model.self_managed_queue = Button_GetCheck(self_managed_check_) == BST_CHECKED;{n}        auto parse_non_negative = [&](HWND edit, const wchar_t* label) -> std::optional<int> {'
assert old10 in text, 'ValidateAndSave not found'
text = text.replace(old10, new10)

# 11. Add HWND members to ModelEditorDialog class
old11 = f'    HWND max_completion_check_ = nullptr;{n}    HWND reasoning_label_ = nullptr;'
new11 = f'    HWND max_completion_check_ = nullptr;{n}    HWND max_active_label_ = nullptr;{n}    HWND max_active_edit_ = nullptr;{n}    HWND max_queue_label_ = nullptr;{n}    HWND max_queue_edit_ = nullptr;{n}    HWND self_managed_check_ = nullptr;{n}    HWND reasoning_label_ = nullptr;'
assert old11 in text, 'ModelEditor HWND members not found'
text = text.replace(old11, new11)

# 12. Add test_queue_button_ to ProviderManagerWindow
old12 = f'    HWND test_connection_button_ = nullptr;{n}    HWND close_button_ = nullptr;'
new12 = f'    HWND test_connection_button_ = nullptr;{n}    HWND test_queue_button_ = nullptr;{n}    HWND close_button_ = nullptr;'
assert old12 in text, 'ProviderManagerWindow HWND members not found'
text = text.replace(old12, new12)

# Write back as UTF-16 LE with BOM
with open('src/provider_manager.cpp', 'wb') as f:
    f.write(codecs.BOM_UTF16_LE + text.encode('utf-16-le'))
print('Done')
