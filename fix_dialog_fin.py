import pathlib

p = pathlib.Path('src/project_settings_dialog.cpp')
lines = p.read_text(encoding='utf-8').splitlines(keepends=True)

def insert_after_line(match, new_lines):
    for i, line in enumerate(lines):
        if match in line:
            for j, nl in enumerate(new_lines):
                lines.insert(i + 1 + j, nl)
            return True
    return False

def replace_line_containing(match, new_line):
    for i, line in enumerate(lines):
        if match in line:
            lines[i] = new_line
            return True
    return False

# 1. Add control IDs after kQuestionnaireModeCombo = 6485,
ok = insert_after_line('    kQuestionnaireModeCombo = 6485,', [
    '    kFilesystemEnabledCheck = 6494,\n',
    '    kFilesystemAutoArchiveCheck = 6495,\n',
    '    kFilesystemWorkDirLabel = 6496,\n',
    '    kFilesystemWorkDirEdit = 6497,\n',
    '    kFilesystemNoteLabel = 6498,\n',
])
print('control IDs', ok)

# 2. Add member variables after questionnaire_mode_combo_ = nullptr;
ok = insert_after_line('    HWND questionnaire_mode_combo_ = nullptr;', [
    '    HWND filesystem_enabled_check_ = nullptr;\n',
    '    HWND filesystem_auto_archive_check_ = nullptr;\n',
    '    HWND filesystem_workdir_label_ = nullptr;\n',
    '    HWND filesystem_workdir_edit_ = nullptr;\n',
    '    HWND filesystem_note_label_ = nullptr;\n',
    '\n',
    '    bool filesystem_enabled_ = false;\n',
    '    bool filesystem_auto_archive_ = false;\n',
    '    std::string filesystem_workdir_ = "$ProjectFolder$";\n',
    '\n',
])
print('member vars', ok)

# 3. Add control creation after questionnaire_mode_combo_ creation
ok = insert_after_line('        questionnaire_mode_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr,', [
    '\n',
    '        filesystem_enabled_check_ = CreateWindowExW(0, L"BUTTON", L"Enable Project Filesystem",\n',
    '            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,\n',
    '            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kFilesystemEnabledCheck), nullptr, nullptr);\n',
    '        filesystem_auto_archive_check_ = CreateWindowExW(0, L"BUTTON", L"Auto-archive reads/writes into Artifact/Code Memory",\n',
    '            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,\n',
    '            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kFilesystemAutoArchiveCheck), nullptr, nullptr);\n',
    '        filesystem_workdir_label_ = CreateWindowExW(0, L"STATIC", L"Working directory:", WS_CHILD | WS_VISIBLE,\n',
    '            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kFilesystemWorkDirLabel), nullptr, nullptr);\n',
    '        filesystem_workdir_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"$ProjectFolder$",\n',
    '            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,\n',
    '            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kFilesystemWorkDirEdit), nullptr, nullptr);\n',
    '        filesystem_note_label_ = CreateWindowExW(0, L"STATIC",\n',
    '            L"Built-in filesystem replaces the MCP file server for this project. Backups go into .agent/backups.",\n',
    '            WS_CHILD | WS_VISIBLE,\n',
    '            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kFilesystemNoteLabel), nullptr, nullptr);\n',
])
print('control creation', ok)

# 4. Add to font array after questionnaire_mode_combo_
ok = replace_line_containing(
    '            questionnaire_restrict_mode_check_, questionnaire_mode_label_, questionnaire_mode_combo_,',
    '            questionnaire_restrict_mode_check_, questionnaire_mode_label_, questionnaire_mode_combo_,\n            filesystem_enabled_check_, filesystem_auto_archive_check_, filesystem_workdir_label_, filesystem_workdir_edit_, filesystem_note_label_,'
)
print('font array', ok)

# 5. InitializeControls: add filesystem init after RefreshQuestionnaireControls();
ok = insert_after_line('        RefreshQuestionnaireControls();', [
    '\n',
    '        filesystem_enabled_ = options_.built_in_filesystem_enabled;\n',
    '        filesystem_auto_archive_ = options_.built_in_filesystem_auto_archive;\n',
    '        filesystem_workdir_ = Trim(options_.built_in_filesystem_working_directory).empty()\n',
    '            ? std::string("$ProjectFolder$")\n',
    '            : options_.built_in_filesystem_working_directory;\n',
    '        if (filesystem_enabled_) {\n',
    '            CheckDlgButton(scroll_content_host_, kFilesystemEnabledCheck, BST_CHECKED);\n',
    '        }\n',
    '        if (filesystem_auto_archive_) {\n',
    '            CheckDlgButton(scroll_content_host_, kFilesystemAutoArchiveCheck, BST_CHECKED);\n',
    '        }\n',
    '        SetWindowTextW(filesystem_workdir_edit_, Utf8ToWide(filesystem_workdir_).c_str());\n',
])
print('init controls', ok)

# 6. LayoutControls: add filesystem MoveWindow after questionnaire_mode_combo_ SendMessage
ok = insert_after_line('        SendMessageW(questionnaire_mode_combo_, CB_SETDROPPEDWIDTH, questionnaire_mode_combo_w, 0);', [
    '\n',
    '        MoveWindow(filesystem_enabled_check_, internal_settings_x + panel_pad, tool_y, internal_settings_w - panel_pad * 2, Scale(hwnd_, 20), TRUE);\n',
    '        MoveWindow(filesystem_auto_archive_check_, internal_settings_x + panel_pad, tool_y + Scale(hwnd_, 25), internal_settings_w - panel_pad * 2, Scale(hwnd_, 20), TRUE);\n',
    '        MoveWindow(filesystem_workdir_label_, internal_settings_x + panel_pad, tool_y + Scale(hwnd_, 50), Scale(hwnd_, 110), label_height, TRUE);\n',
    '        MoveWindow(filesystem_workdir_edit_, internal_settings_x + panel_pad + Scale(hwnd_, 112), tool_y + Scale(hwnd_, 47), internal_settings_w - panel_pad * 2 - Scale(hwnd_, 112), Scale(hwnd_, 22), TRUE);\n',
    '        MoveWindow(filesystem_note_label_, internal_settings_x + panel_pad, tool_y + Scale(hwnd_, 72), internal_settings_w - panel_pad * 2, label_height, TRUE);\n',
])
print('layout controls', ok)

# 7. OnCommand handlers after kQuestionnaireModeCombo break
ok = insert_after_line('        case kQuestionnaireModeCombo:', [
    '            break;\n',
    '        case kFilesystemEnabledCheck:\n',
    '            if (notification_code == BN_CLICKED \u0026\u0026 !toggling_internal_tool_) {\n',
    '                filesystem_enabled_ = (IsDlgButtonChecked(scroll_content_host_, kFilesystemEnabledCheck) == BST_CHECKED);\n',
    '                RefreshInternalToolsList();\n',
    '            }\n',
    '            break;\n',
    '        case kFilesystemAutoArchiveCheck:\n',
    '            if (notification_code == BN_CLICKED \u0026\u0026 !toggling_internal_tool_) {\n',
    '                filesystem_auto_archive_ = (IsDlgButtonChecked(scroll_content_host_, kFilesystemAutoArchiveCheck) == BST_CHECKED);\n',
    '                RefreshInternalToolsList();\n',
    '            }\n',
    '            break;\n',
])
# Remove the original duplicate break line that was after kQuestionnaireModeCombo:
for i, line in enumerate(lines):
    if 'case kQuestionnaireModeCombo:' in line and i + 1 < len(lines) and 'break;' in lines[i + 1]:
        # Already inserted after, so the old break is still there; remove it
        pass
print('oncommand', ok)

# 8. CollectCurrentSettings after result.questionnaire_allowed_mode_id
ok = insert_after_line('            result.questionnaire_allowed_mode_id = std::move(q_mode_id);', [
    '\n',
    '          result.built_in_filesystem_enabled = filesystem_enabled_;\n',
    '          result.built_in_filesystem_auto_archive = filesystem_auto_archive_;\n',
    '          result.built_in_filesystem_working_directory = filesystem_workdir_;\n',
])
print('collect settings', ok)

# 9. SelectInternalTool hide list after questionnaire_mode_combo_
ok = insert_after_line('        ShowWindow(questionnaire_mode_combo_, SW_HIDE);', [
    '        ShowWindow(filesystem_enabled_check_, SW_HIDE);\n',
    '        ShowWindow(filesystem_auto_archive_check_, SW_HIDE);\n',
    '        ShowWindow(filesystem_workdir_label_, SW_HIDE);\n',
    '        ShowWindow(filesystem_workdir_edit_, SW_HIDE);\n',
    '        ShowWindow(filesystem_note_label_, SW_HIDE);\n',
])
print('select hide', ok)

# 10. RefreshInternalToolsList add item after Completion Driver
ok = insert_after_line('            (std::wstring(completion_driver_enabled_ ? L"[✓] " : L"[ ] ") + L"Completion Driver").c_str());', [
    '\n',
    '        ListBox_AddString(internal_tools_list_,\n',
    '            (std::wstring(filesystem_enabled_ ? L"[✓] " : L"[ ] ") + L"Project Filesystem").c_str());\n',
])
print('refresh list insert', ok)

# 11. Fix sel bound: if (sel < 0 || sel > 4) sel = 0;
ok = replace_line_containing('        if (sel < 0 || sel > 4) sel = 0;', '        if (sel < 0 || sel > 5) sel = 0;\n')
print('sel bound', ok)

# 12. ToggleInternalToolEnabled bounds and add case 5
ok = replace_line_containing('        if (index < 0 || index > 4) return;', '        if (index < 0 || index > 5) return;\n')
print('toggle bound', ok)

# Insert case 5 after completion_driver toggle
ok = insert_after_line('            completion_driver_enabled_ = !completion_driver_enabled_;', [
    '        } else if (index == 5) {\n',
    '            filesystem_enabled_ = !filesystem_enabled_;\n',
])
print('toggle case 5', ok)

# 13. ShowInternalToolSettings: add case 5 before "if (panel_has_content)"
ok = replace_line_containing('        if (panel_has_content) {', '''        } else if (index == 5) {
            panel_has_content = true;
            ShowWindow(filesystem_enabled_check_, SW_SHOW);
            ShowWindow(filesystem_auto_archive_check_, SW_SHOW);
            ShowWindow(filesystem_workdir_label_, SW_SHOW);
            ShowWindow(filesystem_workdir_edit_, SW_SHOW);
            ShowWindow(filesystem_note_label_, SW_SHOW);
            CheckDlgButton(scroll_content_host_, kFilesystemEnabledCheck,
                filesystem_enabled_ ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(scroll_content_host_, kFilesystemAutoArchiveCheck,
                filesystem_auto_archive_ ? BST_CHECKED : BST_UNCHECKED);
            SetWindowTextW(filesystem_workdir_edit_, Utf8ToWide(filesystem_workdir_).c_str());
        }
        if (panel_has_content) {''')
print('show settings case 5', ok)

# 14. ShowInternalToolSettings switch title case 5
ok = replace_line_containing('            case 4: title = L"Completion Driver Settings"; break;', '            case 4: title = L"Completion Driver Settings"; break;\n            case 5: title = L"Project Filesystem Settings"; break;')
print('title switch', ok)

# 15. SaveCurrentInternalToolSettings add filesystem case 5
ok = insert_after_line('            questionnaire_enabled_ =\n                (IsDlgButtonChecked(scroll_content_host_, kQuestionnaireEnabledCheck) == BST_CHECKED);', [
    '        }\n',
    '        if (selected_internal_tool_index_ == 5 \u0026\u0026 filesystem_enabled_check_) {\n',
    '            filesystem_enabled_ =\n',
    '                (IsDlgButtonChecked(scroll_content_host_, kFilesystemEnabledCheck) == BST_CHECKED);\n',
    '            filesystem_auto_archive_ =\n',
    '                (IsDlgButtonChecked(scroll_content_host_, kFilesystemAutoArchiveCheck) == BST_CHECKED);\n',
    '            filesystem_workdir_ = Trim(WideToUtf8(GetWindowTextString(filesystem_workdir_edit_)));\n',
    '            if (filesystem_workdir_.empty()) {\n',
    '                filesystem_workdir_ = "$ProjectFolder$";\n',
    '            }\n',
])
print('save current settings', ok)

p.write_text(''.join(lines), encoding='utf-8')
print('Done.')
