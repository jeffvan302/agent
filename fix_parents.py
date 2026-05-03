import re

filepath = r'C:\Users\TheunisvanNiekerk\Code\agent\src\project_settings_dialog.cpp'
with open(filepath, 'r', encoding='utf-8') as f:
    lines = f.readlines()

right_panel_controls = [
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
]

modified = []
in_on_create = False
for i, line in enumerate(lines):
    stripped = line.strip()
    if 'void OnCreate()' in stripped:
        in_on_create = True
    if in_on_create and stripped.startswith('void LayoutControls'):
        in_on_create = False
    
    if in_on_create and ' = CreateWindowExW(' in line:
        for ctrl in right_panel_controls:
            if line.strip().startswith(ctrl + ' = CreateWindowExW('):
                # Replace , hwnd_, with , scroll_content_host_,
                # Be careful not to replace other hwnd_ occurrences if any (usually the parent is the last arg before HMENU)
                # Pattern: ..., hwnd_, reinterpret_cast<HMENU>
                line = line.replace(', hwnd_,', ', scroll_content_host_,')
                break
    modified.append(line)

with open(filepath, 'w', encoding='utf-8') as f:
    f.writelines(modified)

print('Done changing parents.')
print('Modified lines:', sum(1 for a, b in zip(lines, modified) if a != b))
