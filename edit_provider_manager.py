import codecs

input_path = 'src/provider_manager.cpp'
output_path = 'src/provider_manager.cpp'

# Read UTF-16 LE with BOM
with open(input_path, 'rb') as f:
    raw = f.read()
    content = raw.decode('utf-16-le')

# Helper: ensure replacement strings use the same line endings as the file
if '\r\n' in content[:2000]:
    nl = '\r\n'
else:
    nl = '\n'

# Ensure BOM at start
if content[0] != '\ufeff':
    content = '\ufeff' + content

# Edit 1: Add kTestQueue enum
content = content.replace(
    'kTestConnection = 2009,' + nl + '    kStatusLabel = 2010,' + nl + '    kCloseButton = 2011,',
    'kTestConnection = 2009,' + nl + '    kTestQueue = 2010,' + nl + '    kStatusLabel = 2011,' + nl + '    kCloseButton = 2012,'
)

# Edit 2: Add test_queue_button_ creation (before close_button_)
content = content.replace(
    'test_connection_button_ = CreateWindowExW(0, L"BUTTON", L"Test Connection", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kTestConnection), nullptr, nullptr);' + nl + '        close_button_ = CreateWindowExW(0, L"BUTTON", L"Close"',
    'test_connection_button_ = CreateWindowExW(0, L"BUTTON", L"Test Connection", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kTestConnection), nullptr, nullptr);' + nl + '        test_queue_button_ = CreateWindowExW(0, L"BUTTON", L"Test Queue", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kTestQueue), nullptr, nullptr);' + nl + '        close_button_ = CreateWindowExW(0, L"BUTTON", L"Close"'
)

# Edit 3: Add test_queue_button_ to font loop
content = content.replace(
    'test_connection_button_, close_button_, status_label_})',
    'test_connection_button_, test_queue_button_, close_button_, status_label_})'
)

# Edit 4: Add kTestQueue layout
content = content.replace(
    'MoveWindow(test_connection_button_, width - margin - Scale(hwnd_, 210), footer_top - Scale(hwnd_, 4), Scale(hwnd_, 110), button_height, TRUE);' + nl + '        MoveWindow(close_button_, width - margin - Scale(hwnd_, 90), footer_top - Scale(hwnd_, 4), Scale(hwnd_, 90), button_height, TRUE);',
    'MoveWindow(test_connection_button_, width - margin - Scale(hwnd_, 310), footer_top - Scale(hwnd_, 4), Scale(hwnd_, 100), button_height, TRUE);' + nl + '        MoveWindow(test_queue_button_, width - margin - Scale(hwnd_, 200), footer_top - Scale(hwnd_, 4), Scale(hwnd_, 100), button_height, TRUE);' + nl + '        MoveWindow(close_button_, width - margin - Scale(hwnd_, 90), footer_top - Scale(hwnd_, 4), Scale(hwnd_, 90), button_height, TRUE);'
)

# Edit 5: Add kTestQueue command
old = '        case kCloseButton:' + nl + '            DestroyWindow(hwnd_);' + nl + '            break;'
new = '        case kTestQueue:' + nl + '            MessageBoxW(hwnd_, L"Queue test not yet implemented.", L"Test Queue", MB_OK);' + nl + '            break;' + nl + '        case kCloseButton:' + nl + '            DestroyWindow(hwnd_);' + nl + '            break;'
content = content.replace(old, new)

# Edit 6: Add HWND member
content = content.replace(
    '    HWND test_connection_button_ = nullptr;' + nl + '    HWND close_button_ = nullptr;',
    '    HWND test_connection_button_ = nullptr;' + nl + '    HWND test_queue_button_ = nullptr;' + nl + '    HWND close_button_ = nullptr;'
)

# Edit 7: Model Editor control IDs
content = content.replace(
    '    kModelEditorOllamaPull = 2340,' + nl + '    kModelEditorSave = IDOK,',
    '    kModelEditorOllamaPull = 2340,' + nl + '    kModelEditorMaxActiveLabel = 2341,' + nl + '    kModelEditorMaxActiveEdit = 2342,' + nl + '    kModelEditorMaxQueueLabel = 2343,' + nl + '    kModelEditorMaxQueueEdit = 2344,' + nl + '    kModelEditorSelfManagedCheck = 2345,' + nl + '    kModelEditorSave = IDOK,'
)

# Edit 8: Model Editor OnCreate
old = ('        max_completion_check_ = CreateButton(L"Prefer max_completion_tokens", kModelEditorMaxCompletionCheck, BS_AUTOCHECKBOX);' + nl +
       '        Button_SetCheck(max_completion_check_, model_.prefer_max_completion_tokens ? BST_CHECKED : BST_UNCHECKED);' + nl +
       '        reasoning_label_ = CreateLabel(L"Reasoning effort:", kModelEditorReasoningLabel);')
new = ('        max_completion_check_ = CreateButton(L"Prefer max_completion_tokens", kModelEditorMaxCompletionCheck, BS_AUTOCHECKBOX);' + nl +
       '        Button_SetCheck(max_completion_check_, model_.prefer_max_completion_tokens ? BST_CHECKED : BST_UNCHECKED);' + nl +
       '        max_active_label_ = CreateLabel(L"Max active requests (0=no limit):", kModelEditorMaxActiveLabel);' + nl +
       '        max_active_edit_ = CreateEdit(std::to_wstring(std::max(0, model_.max_active_requests)).c_str(), kModelEditorMaxActiveEdit, ES_NUMBER);' + nl +
       '        max_queue_label_ = CreateLabel(L"Max queue size (0=no limit):", kModelEditorMaxQueueLabel);' + nl +
       '        max_queue_edit_ = CreateEdit(std::to_wstring(std::max(0, model_.max_queue_size)).c_str(), kModelEditorMaxQueueEdit, ES_NUMBER);' + nl +
       '        self_managed_check_ = CreateButton(L"Self-managed queue", kModelEditorSelfManagedCheck, BS_AUTOCHECKBOX);' + nl +
       '        Button_SetCheck(self_managed_check_, model_.self_managed_queue ? BST_CHECKED : BST_UNCHECKED);' + nl +
       '        reasoning_label_ = CreateLabel(L"Reasoning effort:", kModelEditorReasoningLabel);')
content = content.replace(old, new)

# Edit 9: Add to font loop
target = '                move_target_up_button_, move_target_down_button_, save_button_, cancel_button_})'
replace = '                move_target_up_button_, move_target_down_button_, save_button_, cancel_button_, max_active_label_, max_active_edit_, max_queue_label_, max_queue_edit_, self_managed_check_})'
content = content.replace(target, replace)

# Edit 10: ValidateAndSave - save fields
content = content.replace(
    '        model.supports_thinking = Button_GetCheck(thinking_check_) == BST_CHECKED;',
    '        model.supports_thinking = Button_GetCheck(thinking_check_) == BST_CHECKED;' + nl + '        model.max_active_requests = std::max(0, ParseInt(GetWindowTextString(max_active_edit_)).value_or(0));' + nl + '        model.max_queue_size = std::max(0, ParseInt(GetWindowTextString(max_queue_edit_)).value_or(0));' + nl + '        model.self_managed_queue = Button_GetCheck(self_managed_check_) == BST_CHECKED;'
)

# Edit 11: LayoutControls - add layout for new model fields
old = '        MoveWindow(max_completion_check_, margin + label_width, y, Scale(220), edit_height, TRUE);' + nl + '        y += edit_height + gutter;'
new = ('        MoveWindow(max_completion_check_, margin + label_width, y, Scale(220), edit_height, TRUE);' + nl +
       '        y += edit_height + gutter;' + nl +
       '        MoveWindow(max_active_label_, margin, y + Scale(4), Scale(220), label_height, TRUE);' + nl +
       '        MoveWindow(max_active_edit_, margin + Scale(226), y, Scale(90), edit_height, TRUE);' + nl +
       '        MoveWindow(max_queue_label_, margin + Scale(336), y + Scale(4), Scale(205), label_height, TRUE);' + nl +
       '        MoveWindow(max_queue_edit_, margin + Scale(544), y, Scale(90), edit_height, TRUE);' + nl +
       '        y += edit_height + gutter;' + nl +
       '        MoveWindow(self_managed_check_, margin + label_width, y, Scale(180), edit_height, TRUE);' + nl +
       '        y += edit_height + gutter;')
content = content.replace(old, new)

# Edit 12: Add HWND members
content = content.replace(
    '    HWND cancel_button_ = nullptr;' + nl + '};',
    '    HWND cancel_button_ = nullptr;' + nl + '    HWND max_active_label_ = nullptr;' + nl + '    HWND max_active_edit_ = nullptr;' + nl + '    HWND max_queue_label_ = nullptr;' + nl + '    HWND max_queue_edit_ = nullptr;' + nl + '    HWND self_managed_check_ = nullptr;' + nl + '};'
)

# Write back as UTF-16 LE with BOM
with open(output_path, 'wb') as f:
    f.write(content.encode('utf-16-le'))

print('Done. Written', len(content.encode('utf-16-le')), 'bytes')