import json

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

m = msgs[411]
content = m.get('content', '')
prefix = 'Read file: README.md\n\n'
if content.startswith(prefix):
    content = content[len(prefix):]
file_lines = content.split('\n')

m2 = msgs[412]
tc = m2.get('tool_calls', [])
for t in tc:
    fn = t.get('function', {})
    if fn.get('name') == 'project_filesystem':
        args = json.loads(fn.get('arguments', ''))
        for i, edit in enumerate(args.get('edits', [])):
            old_lines_str = edit.get('old_lines', [''])[0]
            old_lines = old_lines_str.split('\n')
            print('old_lines count:', len(old_lines))
            print('file_lines count:', len(file_lines))
            print()
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
                print('  Checking line-by-line near expected position...')
                for j in range(68, 80):
                    if j + len(old_lines) <= len(file_lines):
                        mismatches = []
                        for k in range(len(old_lines)):
                            if file_lines[j+k] != old_lines[k]:
                                mismatches.append((j+k, repr(file_lines[j+k]), repr(old_lines[k])))
                        if len(mismatches) <= 3:
                            print(f'  Near match at {j} with {len(mismatches)} mismatches:')
                            for idx, fl, ol in mismatches:
                                print(f'    file[{idx}]={fl} != old[{idx-j}]={ol}')
