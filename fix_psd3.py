import re

filepath = r'C:\Users\TheunisvanNiekerk\Code\agent\src\project_settings_dialog.cpp'
with open(filepath, 'r', encoding='utf-8') as f:
    lines = f.readlines()

in_layout = False
brace_depth = 0
modified = []
for line in lines:
    if 'void LayoutControls(int width, int height)' in line:
        in_layout = True
    if in_layout:
        # Track braces to know when we leave the function
        for ch in line:
            if ch == '{':
                brace_depth += 1
            elif ch == '}':
                brace_depth -= 1
        # Apply replacements inside the function body only
        # Replace exact ', right_x,' with ', 0,'
        line = line.replace(', right_x,', ', 0,')
        # Replace '= right_x +' with '= '
        line = line.replace('= right_x +', '= ')
        # Replace any remaining 'right_x +' when preceded by comma/space etc.
        line = re.sub(r'(?<=[,\s])right_x \+ ', '', line)
    if in_layout and brace_depth == 0 and 'void LayoutControls' not in line:
        in_layout = False
    modified.append(line)

with open(filepath, 'w', encoding='utf-8') as f:
    f.writelines(modified)

print('Done adjusting LayoutControls coordinates.')
