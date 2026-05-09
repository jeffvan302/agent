import json

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

for idx in [307]:
    m = msgs[idx]
    tc = m.get('tool_calls', [])
    for t in tc:
        fn = t.get('function', {})
        if fn.get('name') == 'project_filesystem':
            args_str = fn.get('arguments', '')
            args = json.loads(args_str)
            print('=== [' + str(idx) + '] ===')
            print('action=' + args.get('action', ''))
            print('file=' + args.get('file', ''))
            for i, edit in enumerate(args.get('edits', [])):
                ol = edit.get('old_lines', [])
                nl = edit.get('new_lines', [])
                print('edit[' + str(i) + '] old_lines:')
                for line in ol:
                    print('  ' + repr(line)[:100])
                print('edit[' + str(i) + '] new_lines (first 2):')
                for line in nl[:2]:
                    print('  ' + repr(line)[:100])
            print()
