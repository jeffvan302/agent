with open(r'C:\Temp\Creating_Applications\admin\Test_Face\README.md', 'r', encoding='utf-8') as f:
    file_content = f.read()
file_lines = file_content.split('\n')

# old_lines from message [412]
old_lines_str = "### Dependencies\n\nIf you only want to install the runtime dependencies without the package:\n\n```bash\npip install -r requirements.txt\n```\n\n> **Note**: InsightFace model files are downloaded automatically on first use if not already present. They are cached in `~/.insightface/models/`."

split_old = old_lines_str.split('\n')
print('split_old length:', len(split_old))
for i, line in enumerate(split_old):
    print(f'  [{i}]: {repr(line)}')

print()
print('file_lines around 84-95:')
for i in range(84, 96):
    print(f'  [{i}]: {repr(file_lines[i])}')

print()
print('Trying exact match...')
for i in range(len(file_lines) - len(split_old) + 1):
    match = True
    for j in range(len(split_old)):
        if file_lines[i+j] != split_old[j]:
            match = False
            break
    if match:
        print(f'Exact match at file line {i}')
        for j in range(len(split_old)):
            print(f'  file[{i+j}] == old[{j}]: {repr(file_lines[i+j])}')
        break
else:
    print('No exact match')

print()
print('Trying trimmed match...')
for i in range(len(file_lines) - len(split_old) + 1):
    match = True
    for j in range(len(split_old)):
        if file_lines[i+j].rstrip(' \t\r') != split_old[j].rstrip(' \t\r'):
            match = False
            break
    if match:
        print(f'Trimmed match at file line {i}')
        break
else:
    print('No trimmed match')
