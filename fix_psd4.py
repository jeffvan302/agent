import re

filepath = r'C:\Users\TheunisvanNiekerk\Code\agent\src\project_settings_dialog.cpp'
with open(filepath, 'r', encoding='utf-8') as f:
    lines = f.readlines()

ids_to_fix = {
    'kManualContextCompressionCheck',
    'kInternalToolsHeader',
    'kInternalToolsList',
    'kInternalToolSettingsPanel',
    'kInternalPowerShellEnabled',
    'kInternalPowerShellWorkingDirLabel',
    'kInternalPowerShellWorkingDirEdit',
    'kInternalPowerShellRiskLabel',
    'kRagServicesList',
    'kProjVarsList',
    'kProjVarsAdd',
    'kProjVarsNameLabel',
    'kProjVarsNameEdit',
    'kProjVarsValueEdit',
    'kAgenticModesList',
    'kChatLoggingCheck',
    'kWebDebuggingCheck',
    'kInstructionsEdit',
}

in_on_create = False
modified = []
for line in lines:
    if 'void OnCreate()' in line:
        in_on_create = True
    if in_on_create and line.strip().startswith('void LayoutControls'):
        in_on_create = False
    if in_on_create and 'CreateWindowExW' in line and ', hwnd_, reinterpret_cast<HMENU>(' in line:
        for cid in ids_to_fix:
            if f'HMENU>({cid}), nullptr, nullptr);' in line:
                line = line.replace(', hwnd_, reinterpret_cast<HMENU>(', ', scroll_content_host_, reinterpret_cast<HMENU>(', 1)
                break
    modified.append(line)

with open(filepath, 'w', encoding='utf-8') as f:
    f.writelines(modified)
print('Fixed parent hwnd -> scroll_content_host for remaining right-panel controls in OnCreate.')
