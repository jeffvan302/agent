import json

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

# Get old_lines for [412]
tc = msgs[412].get('tool_calls', [])
args = json.loads(tc[0]['function']['arguments'])
old_lines = args['edits'][0]['old_lines']

# Read actual README.md
with open(r'C:\Temp\Creating_Applications\admin\Test_Face\README.md', 'r', encoding='utf-8') as f:
    file_content = f.read()

file_lines = file_content.split('\n')

print('File lines around Note (with repr):')
for i in range(92, 97):
    print(f'  [{i}]: {repr(file_lines[i])}')

print()
print('old_lines from model:')
for i, line in enumerate(old_lines):
    print(f'  [{i}]: {repr(line)}')

# Split old_lines[0] by \n since it's a multiline string
if old_lines:
    split_old = old_lines[0].split('\n')
    print()
    print('Split old_lines[0]:')
    for i, line in enumerate(split_old):
        print(f'  [{i}]: {repr(line)}')
    
    # Try exact match
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
            break
    else:
        print('No exact match')
    
    # Try trimmed match
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
