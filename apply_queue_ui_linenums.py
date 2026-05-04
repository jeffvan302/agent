import codecs

path = 'src/provider_manager.cpp'
with open(path, 'rb') as f:
    raw = f.read()
content = raw.decode('utf-16-le')
lines = content.split('\r\n')

# 1. Add kTestQueue enum
for i, line in enumerate(lines):
    if 'kTestConnection = 2009,' in line:
        lines.insert(i+1, '    kTestQueue = 2010,')
        break

# 2. Shift status label and close button IDs
for i, line in enumerate(lines):
    if 'kStatusLabel = 2010,' in line:
        lines[i] = '    kStatusLabel = 2011,'
    if 'kCloseButton = 2011,' in line:
        lines[i] = '    kCloseButton = 2012,'

# 3. Add test_queue_button_ creation in OnCreate
for i, line in enumerate(lines):
    if 'test_connection_button_ = CreateWindowExW(0, L"BUTTON", L"Test Connection"' in line:
        lines.insert(i+1, '        test_queue_button_ = CreateWindowExW(0, L"BUTTON", L"Test Queue", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kTestQueue), nullptr, nullptr);')
        break

# 4. Add to font loop
for i, line in enumerate(lines):
    if 'test_connection_button_, close_button_, status_label_})' in line:
        lines[i] = line.replace('test_connection_button_, close_button_, status_label_})', 'test_connection_button_, test_queue_button_, close_button_, status_label_}')
        break

# 5. Adjust footer layout
for i, line in enumerate(lines):
    if 'MoveWindow(test_connection_button_, width - margin - Scale(hwnd_, 210)' in line:
        lines[i] = '        MoveWindow(test_connection_button_, width - margin - Scale(hwnd_, 310), footer_top - Scale(hwnd_, 4), Scale(hwnd_, 100), button_height, TRUE);'
        lines.insert(i+1, '        MoveWindow(test_queue_button_, width - margin - Scale(hwnd_, 200), footer_top - Scale(hwnd_, 4), Scale(hwnd_, 100), button_height, TRUE);')
        # Fix close button position
        if i+2 < len(lines) and 'MoveWindow(close_button_, width - margin - Scale(hwnd_, 90)' in lines[i+2]:
            pass  # Already correct
        break

# 6. Add command handler
for i, line in enumerate(lines):
    if 'case kCloseButton:' in line:
        lines.insert(i, '        case kTestQueue:')
        lines.insert(i+1, '            MessageBoxW(hwnd_, L"Queue test not yet implemented.", L"Test Queue", MB_OK);')
        lines.insert(i+2, '            break;')
        break

# 7. Add HWND member
for i, line in enumerate(lines):
    if 'HWND test_connection_button_ = nullptr;' in line:
        lines.insert(i+1, '    HWND test_queue_button_ = nullptr;')
        break

# 8. Add model editor enum IDs
for i, line in enumerate(lines):
    if 'kModelEditorOllamaPull = 2340,' in line:
        lines.insert(i+1, '    kModelEditorMaxActiveLabel = 2341,')
        lines.insert(i+2, '    kModelEditorMaxActiveEdit = 2342,')
        lines.insert(i+3, '    kModelEditorMaxQueueLabel = 2343,')
        lines.insert(i+4, '    kModelEditorMaxQueueEdit = 2344,')
        lines.insert(i+5, '    kModelEditorSelfManagedCheck = 2345,')
        break

# 9. Add OnCreate controls after max_completion_check_
for i, line in enumerate(lines):
    if 'max_completion_check_ = CreateButton(L"Prefer max_completion_tokens", kModelEditorMaxCompletionCheck, BS_AUTOCHECKBOX);' in line:
        j = i + 1
        # Insert after the Button_SetCheck for max_completion
        while j < len(lines) and 'reasoning_label_' not in lines[j]:
            j += 1
        if j < len(lines):
            new_lines = [
                '        max_active_label_ = CreateLabel(L"Max active requests (0=no limit):", kModelEditorMaxActiveLabel);',
                '        max_active_edit_ = CreateEdit(std::to_wstring(std::max(0, model_.max_active_requests)).c_str(), kModelEditorMaxActiveEdit, ES_NUMBER);',
                '        max_queue_label_ = CreateLabel(L"Max queue size (0=no limit):", kModelEditorMaxQueueLabel);',
                '        max_queue_edit_ = CreateEdit(std::to_wstring(std::max(0, model_.max_queue_size)).c_str(), kModelEditorMaxQueueEdit, ES_NUMBER);',
                '        self_managed_check_ = CreateButton(L"Self-managed queue", kModelEditorSelfManagedCheck, BS_AUTOCHECKBOX);',
                '        Button_SetCheck(self_managed_check_, model_.self_managed_queue ? BST_CHECKED : BST_UNCHECKED);'
            ]
            for nl in reversed(new_lines):
                lines.insert(j, nl)
        break

# 10. Add to ModelEditor font loop
for i, line in enumerate(lines):
    if 'save_button_, cancel_button_})' in line:
        if 'move_target_up_button_' in line:  # ModelEditor loop
            lines[i] = line.replace('save_button_, cancel_button_})', 'save_button_, cancel_button_, max_active_label_, max_active_edit_, max_queue_label_, max_queue_edit_, self_managed_check_}')
            break

# 11. Add ValidateAndSave fields
for i, line in enumerate(lines):
    if 'model.supports_thinking = AskYesNo(hwnd_, L"Should this model be marked as thinking-capable?", L"Thinking Support");' in line:
        lines.insert(i+1, '        model.max_active_requests = std::max(0, ParseInt(GetWindowTextString(max_active_edit_)).value_or(0));')
        lines.insert(i+2, '        model.max_queue_size = std::max(0, ParseInt(GetWindowTextString(max_queue_edit_)).value_or(0));')
        lines.insert(i+3, '        model.self_managed_queue = Button_GetCheck(self_managed_check_) == BST_CHECKED;')
        break

# 12. Add LayoutControls placement
for i, line in enumerate(lines):
    if 'MoveWindow(max_completion_check_, margin + label_width, y, Scale(220), edit_height, TRUE);' in line:
        # Only apply to 2nd occurrence (ModelEditor)
        count = 0
        for j in range(i, len(lines)):
            if 'MoveWindow(max_completion_check_, margin + label_width, y, Scale(220), edit_height, TRUE);' in lines[j]:
                count += 1
                if count == 2:
                    k = j + 1
                    # Should be "y += edit_height + gutter;"
                    new_layout = [
                        '        y += edit_height + gutter;',
                        '        MoveWindow(max_active_label_, margin, y + Scale(4), Scale(220), label_height, TRUE);',
                        '        MoveWindow(max_active_edit_, margin + Scale(226), y, Scale(90), edit_height, TRUE);',
                        '        MoveWindow(max_queue_label_, margin + Scale(336), y + Scale(4), Scale(205), label_height, TRUE);',
                        '        MoveWindow(max_queue_edit_, margin + Scale(544), y, Scale(90), edit_height, TRUE);',
                        '        y += edit_height + gutter;',
                        '        MoveWindow(self_managed_check_, margin + label_width, y, Scale(180), edit_height, TRUE);'
                    ]
                    for nl in reversed(new_layout):
                        lines.insert(k, nl)
                    break
        break

# 13. Add HWND members to ModelEditorDialog
for i, line in enumerate(lines):
    if 'HWND cancel_button_ = nullptr;' in line:
        # Count occurrences - 2nd is ModelEditor
        count = 0
        for j in range(i, len(lines)):
            if 'HWND cancel_button_ = nullptr;' in lines[j]:
                count += 1
                if count == 2:
                    lines.insert(j+1, '    HWND max_active_label_ = nullptr;')
                    lines.insert(j+2, '    HWND max_active_edit_ = nullptr;')
                    lines.insert(j+3, '    HWND max_queue_label_ = nullptr;')
                    lines.insert(j+4, '    HWND max_queue_edit_ = nullptr;')
                    lines.insert(j+5, '    HWND self_managed_check_ = nullptr;')
                    break
        break

output = '\r\n'.join(lines)
if not output.startswith('\ufeff'):
    output = '\ufeff' + output
with open(path, 'wb') as f:
    f.write(output.encode('utf-16-le'))
print('Done. Size:', len(output.encode('utf-16-le')))
