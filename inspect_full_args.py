import json

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

for j in [301, 307, 412]:
    m = msgs[j]
    tc = m.get('tool_calls', [])
    for t in tc:
        fn = t.get('function', {})
        if fn.get('name') == 'project_filesystem':
            args_str = fn.get('arguments', '')
            print('=== [' + str(j) + '] args length=' + str(len(args_str)) + ' ===')
            # Try to parse
            try:
                args = json.loads(args_str)
                for i, edit in enumerate(args.get('edits', [])):
                    print('edit[' + str(i) + ']:')
                    print('  old_lines=' + repr(edit.get('old_lines', [])))
                    print('  new_lines=' + repr(edit.get('new_lines', []))[:300])
            except Exception as e:
                print('Parse error:', e)
                print('Raw args=' + args_str[:500])
    print()
