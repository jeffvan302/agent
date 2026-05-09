import json

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

for idx in [301, 307, 412, 418, 422, 439]:
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
                print('edit[' + str(i) + ']:')
                ol = edit.get('old_lines', [])
                nl = edit.get('new_lines', [])
                print('  old_lines type=' + str(type(ol)))
                if isinstance(ol, list):
                    print('  old_lines is LIST with ' + str(len(ol)) + ' elements')
                    for line in ol:
                        print('    ' + repr(line)[:100])
                elif isinstance(ol, str):
                    print('  old_lines is STRING: ' + repr(ol)[:100])
                print('  new_lines type=' + str(type(nl)))
                if isinstance(nl, list):
                    print('  new_lines is LIST with ' + str(len(nl)) + ' elements')
                    for line in nl:
                        print('    ' + repr(line)[:100])
                elif isinstance(nl, str):
                    print('  new_lines is STRING: ' + repr(nl)[:100])
            print()
