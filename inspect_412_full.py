import json

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

m = msgs[412]
tc = m.get('tool_calls', [])
for t in tc:
    fn = t.get('function', {})
    if fn.get('name') == 'project_filesystem':
        args_str = fn.get('arguments', '')
        args = json.loads(args_str)
        for i, edit in enumerate(args.get('edits', [])):
            ol = edit.get('old_lines', [])
            nl = edit.get('new_lines', [])
            print('=== old_lines ===')
            for j, line in enumerate(ol):
                print(f'[{j}]: {repr(line)}')
            print()
            print('=== new_lines (first 3) ===')
            for j, line in enumerate(nl[:3]):
                print(f'[{j}]: {repr(line)}')
