import codecs

path = 'src/provider_manager.cpp'

with open(path, 'rb') as f:
    raw = f.read()
    content = raw.decode('utf-16-le')

# Ensure BOM
if content[0] != '\ufeff':
    content = '\ufeff' + content

# Use exact CRLF in replacements because the file is Windows-formatted
nl = '\r\n'

# 1. Add kTestQueue enum value
content = content.replace(
    'kTestConnection = 2009,' + nl + '    kStatusLabel = 2010,' + nl + '    kCloseButton = 2011,',
    'kTestConnection = 2009,' + nl + '    kTestQueue = 2010,' + nl + '    kStatusLabel = 2011,' + nl + '    kCloseButton = 2012,'
)

# 2. Add test_queue_button_ creation in ProviderManagerWindow::OnCreate
content = content.replace(
    'test_connection_button_ = CreateWindowExW(0, L"BUTTON", L"Test Connection", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kTestConnection), nullptr, nullptr);' + nl + '        close_button_',
    'test_connection_button_ = CreateWindowExW(0, L"BUTTON", L"Test Connection", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kTestConnection), nullptr, nullptr);' + nl + '        test_queue_button_ = CreateWindowExW(0, L"BUTTON", L"Test Queue", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kTestQueue), nullptr, nullptr);' + nl + '        close_button_'
)

# 3. Add test_queue_button_ to font loop (ProviderManagerWindow)
content = content.replace(
    'test_connection_button_, close_button_, status_label_})',
    'test_connection_button_, test_queue_button_, close_button_, status_label_})'
)

# 4. Adjust footer layout to fit three buttons
content = content.replace(
    'MoveWindow(test_connection_button_, width - margin - Scale(hwnd_, 210), footer_top - Scale(hwnd_, 4), Scale(hwnd_, 110), button_height, TRUE);' + nl + '        MoveWindow(close_button_, width - margin - Scale(hwnd_, 90), footer_top - Scale(hwnd_, 4), Scale(hwnd_, 90), button_height, TRUE);',
    'MoveWindow(test_connection_button_, width - margin - Scale(hwnd_, 310), footer_top - Scale(hwnd_, 4), Scale(hwnd_, 100), button_height, TRUE);' + nl + '        MoveWindow(test_queue_button_, width - margin - Scale(hwnd_, 200), footer_top - Scale(hwnd_, 4), Scale(hwnd_, 100), button_height, TRUE);' + nl + '        MoveWindow(close_button_, width - margin - Scale(hwnd_, 90), footer_top - Scale(hwnd_, 4), Scale(hwnd_, 90), button_height, TRUE);'
)

# 5. Add kTestQueue command handler
old_cmd = '        case kCloseButton:' + nl + '            DestroyWindow(hwnd_);' + nl + '            break;'
new_cmd = '        case kTestQueue:' + nl + '            MessageBoxW(hwnd_, L"Queue test not yet implemented.", L"Test Queue", MB_OK);' + nl + '            break;' + nl + '        case kCloseButton:' + nl + '            DestroyWindow(hwnd_);' + nl + '            break;'
content = content.replace(old_cmd, new_cmd)

# 6. Add HWND member for test_queue_button_ in ProviderManagerWindow
content = content.replace(
    '    HWND test_connection_button_ = nullptr;' + nl + '    HWND close_button_ = nullptr;',
    '    HWND test_connection_button_ = nullptr;' + nl + '    HWND test_queue_button_ = nullptr;' + nl + '    HWND close_button_ = nullptr;'
)

# 7. Add model editor control IDs
content = content.replace(
    '    kModelEditorOllamaPull = 2340,' + nl + '    kModelEditorSave = IDOK,',
    '    kModelEditorOllamaPull = 2340,' + nl + '    kModelEditorMaxActiveLabel = 2341,' + nl + '    kModelEditorMaxActiveEdit = 2342,' + nl + '    kModelEditorMaxQueueLabel = 2343,' + nl + '    kModelEditorMaxQueueEdit = 2344,' + nl + '    kModelEditorSelfManagedCheck = 2345,' + nl + '    kModelEditorSave = IDOK,'
)

# 8. Add OnCreate controls after max_completion_check_
old_oncreate = ('        max_completion_check_ = CreateButton(L"Prefer max_completion_tokens", kModelEditorMaxCompletionCheck, BS_AUTOCHECKBOX);' + nl +
                '        Button_SetCheck(max_completion_check_, model_.prefer_max_completion_tokens ? BST_CHECKED : BST_UNCHECKED);' + nl +
                '        reasoning_label_ = CreateLabel(L"Reasoning effort:", kModelEditorReasoningLabel);')
new_oncreate = ('        max_completion_check_ = CreateButton(L"Prefer max_completion_tokens", kModelEditorMaxCompletionCheck, BS_AUTOCHECKBOX);' + nl +
                '        Button_SetCheck(max_completion_check_, model_.prefer_max_completion_tokens ? BST_CHECKED : BST_UNCHECKED);' + nl +
                '        max_active_label_ = CreateLabel(L"Max active requests (0=no limit):", kModelEditorMaxActiveLabel);' + nl +
                '        max_active_edit_ = CreateEdit(std::to_wstring(std::max(0, model_.max_active_requests)).c_str(), kModelEditorMaxActiveEdit, ES_NUMBER);' + nl +
                '        max_queue_label_ = CreateLabel(L"Max queue size (0=no limit):", kModelEditorMaxQueueLabel);' + nl +
                '        max_queue_edit_ = CreateEdit(std::to_wstring(std::max(0, model_.max_queue_size)).c_str(), kModelEditorMaxQueueEdit, ES_NUMBER);' + nl +
                '        self_managed_check_ = CreateButton(L"Self-managed queue", kModelEditorSelfManagedCheck, BS_AUTOCHECKBOX);' + nl +
                '        Button_SetCheck(self_managed_check_, model_.self_managed_queue ? BST_CHECKED : BST_UNCHECKED);' + nl +
                '        reasoning_label_ = CreateLabel(L"Reasoning effort:", kModelEditorReasoningLabel);')
content = content.replace(old_oncreate, new_oncreate)

# 9. Add to ModelEditor font loop
old_font = '                move_target_up_button_, move_target_down_button_, save_button_, cancel_button_})'
new_font = '                move_target_up_button_, move_target_down_button_, save_button_, cancel_button_, max_active_label_, max_active_edit_, max_queue_label_, max_queue_edit_, self_managed_check_})'
# Only replace the SECOND occurrence (ModelEditor)
idx = content.find(old_font)
if idx != -1:
    idx2 = content.find(old_font, idx + 1)
    if idx2 != -1:
        content = content[:idx2] + new_font + content[idx2 + len(old_font):]

# 10. Add to ValidateAndSave
# Find the unique block in ModelEditor ValidateAndSave
old_validate = '        model.supports_thinking = AskYesNo(hwnd_, L"Should this model be marked as thinking-capable?", L"Thinking Support");' + nl + '        return true;'
new_validate = ('        model.supports_thinking = AskYesNo(hwnd_, L"Should this model be marked as thinking-capable?", L"Thinking Support");' + nl +
                '        model.max_active_requests = std::max(0, ParseInt(GetWindowTextString(max_active_edit_)).value_or(0));' + nl +
                '        model.max_queue_size = std::max(0, ParseInt(GetWindowTextString(max_queue_edit_)).value_or(0));' + nl +
                '        model.self_managed_queue = Button_GetCheck(self_managed_check_) == BST_CHECKED;' + nl +
                '        return true;')
content = content.replace(old_validate, new_validate)

# 11. Add to LayoutControls (ModelEditor)
old_layout = '        MoveWindow(max_completion_check_, margin + label_width, y, Scale(220), edit_height, TRUE);' + nl + '        y += edit_height + gutter;'
new_layout = ('        MoveWindow(max_completion_check_, margin + label_width, y, Scale(220), edit_height, TRUE);' + nl +
              '        y += edit_height + gutter;' + nl +
              '        MoveWindow(max_active_label_, margin, y + Scale(4), Scale(220), label_height, TRUE);' + nl +
              '        MoveWindow(max_active_edit_, margin + Scale(226), y, Scale(90), edit_height, TRUE);' + nl +
              '        MoveWindow(max_queue_label_, margin + Scale(336), y + Scale(4), Scale(205), label_height, TRUE);' + nl +
              '        MoveWindow(max_queue_edit_, margin + Scale(544), y, Scale(90), edit_height, TRUE);' + nl +
              '        y += edit_height + gutter;' + nl +
              '        MoveWindow(self_managed_check_, margin + label_width, y, Scale(180), edit_height, TRUE);' + nl +
              '        y += edit_height + gutter;')
# Only replace the SECOND occurrence (ModelEditor)
idx = content.find(old_layout)
if idx != -1:
    idx2 = content.find(old_layout, idx + 1)
    if idx2 != -1:
        content = content[:idx2] + new_layout + content[idx2 + len(old_layout):]

# 12. Add HWND members in ModelEditorDialog
content = content.replace(
    '    HWND cancel_button_ = nullptr;' + nl + '};',
    '    HWND cancel_button_ = nullptr;' + nl + '    HWND max_active_label_ = nullptr;' + nl + '    HWND max_active_edit_ = nullptr;' + nl + '    HWND max_queue_label_ = nullptr;' + nl + '    HWND max_queue_edit_ = nullptr;' + nl + '    HWND self_managed_check_ = nullptr;' + nl + '};'
)

with open(path, 'wb') as f:
    f.write(content.encode('utf-16-le'))

print('Done. File size:', len(content.encode('utf-16-le')))
