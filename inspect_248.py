import json

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

# Find assistant before [248]
for j in range(247, 240, -1):
    m = msgs[j]
    if m.get('role') == 'assistant':
        print('Assistant at [' + str(j) + ']')
        tc = m.get('tool_calls', [])
        for t in tc:
            fn = t.get('function', {})
            if fn.get('name') == 'project_filesystem':
                args = json.loads(fn.get('arguments', '{}'))
                if args.get('action') == 'edit':
                    print('  File: ' + args.get('file', ''))
                    for i, edit in enumerate(args.get('edits', [])):
                        print('  old_lines:')
                        for line in edit.get('old_lines', []):
                            print('    ' + repr(line))
                        print('  new_lines (first 3):')
                        for line in edit.get('new_lines', [])[:3]:
                            print('    ' + repr(line))
        break
