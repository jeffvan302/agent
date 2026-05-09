import json

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

m = msgs[247]
tc = m.get('tool_calls', [])
for t in tc:
    fn = t.get('function', {})
    if fn.get('name') == 'project_filesystem':
        args_str = fn.get('arguments', '')
        args = json.loads(args_str)
        print('action=' + args.get('action', ''))
        print('file=' + args.get('file', ''))
        for i, edit in enumerate(args.get('edits', [])):
            print('edit[' + str(i) + ']:')
            ol = edit.get('old_lines', [])
            nl = edit.get('new_lines', [])
            print('  old_lines type=' + str(type(ol)))
            if isinstance(ol, list):
                print('  old_lines (list with ' + str(len(ol)) + ' elements):')
                for line in ol:
                    print('    ' + repr(line)[:200])
            elif isinstance(ol, str):
                print('  old_lines (string):')
                print('    ' + repr(ol)[:200])
            print('  new_lines type=' + str(type(nl)))
            if isinstance(nl, list):
                print('  new_lines (list with ' + str(len(nl)) + ' elements):')
                for line in nl:
                    print('    ' + repr(line)[:200])
            elif isinstance(nl, str):
                print('  new_lines (string):')
                print('    ' + repr(nl)[:200])
