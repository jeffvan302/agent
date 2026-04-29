import re

filepath = r'C:\Users\TheunisvanNiekerk\Code\agent\src\project_settings_dialog.cpp'
with open(filepath, 'r', encoding='utf-8') as f:
    text = f.read()

ids_to_fix = [
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
]

for cid in ids_to_fix:
    old = f', hwnd_, reinterpret_cast<HMENU>({cid})'
    new = f', scroll_content_host_, reinterpret_cast<HMENU>({cid})'
    text = text.replace(old, new)

with open(filepath, 'w', encoding='utf-8') as f:
    f.write(text)
print('Fixed multiline hwnd parents for ', len(ids_to_fix), ' controls.')
