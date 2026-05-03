import re

filepath = r'C:\Users\TheunisvanNiekerk\Code\agent\src\project_settings_dialog.cpp'
with open(filepath, 'r', encoding='utf-8') as f:
    text = f.read()

# We want to replace parent hwnd_ with scroll_content_host_ for controls created after scroll panel setup and before footer buttons.
# We'll find the OnCreate() block and within it, after scroll_content_host_ = CreateWindowExW(...);
# replace occurrences in CreateWindowExW( ... , hwnd_, reinterpret_cast<HMENU>(...)
# for controls listed below.

# List of control variable names that belong on scroll_content_host_
right_controls = {
    'model_label_', 'model_combo_',
    'context_window_label_', 'context_window_combo_',
    'manual_context_compression_check_',
    'chat_logging_check_',
    'web_debugging_check_',
    'internal_tools_header_', 'internal_tools_list_', 'internal_tool_settings_panel_',
    'internal_powershell_enabled_check_', 'internal_powershell_workdir_label_',
    'internal_powershell_workdir_edit_', 'internal_powershell_risk_label_',
    'rag_services_header_', 'rag_services_list_',
    'rag_enabled_check_', 'rag_read_check_', 'rag_write_check_', 'rag_tool_check_',
    'rag_delete_check_', 'rag_export_check_', 'rag_default_ingest_check_',
    'rag_priority_label_', 'rag_priority_edit_',
    'rag_max_chunks_label_', 'rag_max_chunks_edit_',
    'rag_min_confidence_label_', 'rag_min_confidence_edit_',
    'rag_max_confidence_label_', 'rag_max_confidence_edit_',
    'rag_export_path_label_', 'rag_export_path_edit_',
    'rag_retrieval_mode_label_', 'rag_retrieval_mode_combo_',
    'proj_vars_header_', 'proj_vars_list_', 'proj_vars_add_btn_', 'proj_vars_remove_btn_',
    'proj_vars_name_label_', 'proj_vars_name_edit_',
    'proj_vars_value_label_', 'proj_vars_value_edit_',
    'proj_vars_description_label_', 'proj_vars_description_edit_', 'proj_vars_inject_check_',
    'agentic_mode_label_', 'agentic_mode_combo_',
    'agentic_modes_list_label_', 'agentic_modes_list_',
    'instructions_label_', 'import_instructions_button_', 'instructions_edit_',
}

# Use regex: match lines that start with optional whitespace then one of the control names then ' = CreateWindowExW('
# and contain ', hwnd_, reinterpret_cast<HMENU>(...'
# Replace the first occurrence of ', hwnd_, reinterpret_cast<HMENU>(' with ', scroll_content_host_, reinterpret_cast<HMENU>('.

def replace_in_line(line):
    stripped = line.lstrip()
    for ctrl in right_controls:
        if stripped.startswith(ctrl + ' = CreateWindowExW('):
            if ', hwnd_, reinterpret_cast<HMENU>(' in line:
                return line.replace(', hwnd_, reinterpret_cast<HMENU>(', ', scroll_content_host_, reinterpret_cast<HMENU>(', 1)
            break
    return line

lines = text.splitlines(keepends=True)
modified = []
in_on_create = False
changed = 0
for line in lines:
    if 'void OnCreate()' in line:
        in_on_create = True
    if in_on_create and line.strip().startswith('void LayoutControls'):
        in_on_create = False
    if in_on_create:
        new_line = replace_in_line(line)
        if new_line is not line:
            changed += 1
        modified.append(new_line)
    else:
        modified.append(line)

with open(filepath, 'w', encoding='utf-8') as f:
    f.writelines(modified)

print(f'Done. Modified {changed} lines.')
