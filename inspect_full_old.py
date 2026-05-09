import json

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

for j in [412, 418, 422, 439]:
    m = msgs[j]
    tc = m.get('tool_calls', [])
    for t in tc:
        fn = t.get('function', {})
        if fn.get('name') == 'project_filesystem':
            args_str = fn.get('arguments', '')
            args = json.loads(args_str)
            print('=== [' + str(j) + '] ===')
            for i, edit in enumerate(args.get('edits', [])):
                print('edit[' + str(i) + '] old_lines:')
                for line in edit.get('old_lines', []):
                    print('  ' + repr(line))
                print('edit[' + str(i) + '] new_lines (first 2):')
                for line in edit.get('new_lines', [])[:2]:
                    print('  ' + repr(line))
            print()
