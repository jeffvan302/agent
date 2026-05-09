import json

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

m = msgs[411]
content = m.get('content', '')
prefix = 'Read file: README.md\n\n'
if content.startswith(prefix):
    content = content[len(prefix):]

# Mimic std::getline behavior (split on \n, keep \r)
def cpp_getlines(text):
    lines = []
    current = ''
    for ch in text:
        if ch == '\n':
            lines.append(current)
            current = ''
        else:
            current += ch
    # std::getline does NOT push an extra empty line if text ends with \n
    # But if text doesn't end with \n, it pushes the last line
    if current or not text.endswith('\n'):
        lines.append(current)
    return lines

file_lines = cpp_getlines(content)
print(f'cpp_getlines count: {len(file_lines)}')

m2 = msgs[412]
tc = m2.get('tool_calls', [])
for t in tc:
    fn = t.get('function', {})
    if fn.get('name') == 'project_filesystem':
        args = json.loads(fn.get('arguments', ''))
        for i, edit in enumerate(args.get('edits', [])):
            old_lines_str = edit.get('old_lines', [''])[0]
            
            # Mimic SplitFilesystemEditLineItem
            def split_fs(value):
                lines = []
                current = ''
                saw_break = False
                i = 0
                while i < len(value):
                    ch = value[i]
                    if ch == '\r' or ch == '\n':
                        saw_break = True
                        lines.append(current)
                        current = ''
                        if ch == '\r' and i + 1 < len(value) and value[i+1] == '\n':
                            i += 1
                        i += 1
                        continue
                    current += ch
                    i += 1
                if not saw_break or current:
                    lines.append(current)
                if not lines:
                    lines.append('')
                return lines
            
            old_lines = split_fs(old_lines_str)
            print(f'old_lines count: {len(old_lines)}')
            
            print('Trying exact match...')
            for j in range(len(file_lines) - len(old_lines) + 1):
                match = True
                for k in range(len(old_lines)):
                    if file_lines[j+k] != old_lines[k]:
                        match = False
                        break
                if match:
                    print(f'Exact match at file line {j}')
                    break
            else:
                print('No exact match')
                
                print('  Checking near expected position...')
                for j in range(65, min(75, len(file_lines) - len(old_lines) + 1)):
                    mismatches = []
                    for k in range(len(old_lines)):
                        if j+k < len(file_lines) and file_lines[j+k] != old_lines[k]:
                            mismatches.append((j+k, repr(file_lines[j+k]), repr(old_lines[k])))
                    if mismatches:
                        print(f'  At {j}: {len(mismatches)} mismatches')
                        for idx, fl, ol in mismatches[:3]:
                            print(f'    file[{idx}]={fl}')
                            print(f'    old[{idx-j}]={ol}')
