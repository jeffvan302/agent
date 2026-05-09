import json

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

m = msgs[410]
print('role=' + m.get('role'))
print('name=' + m.get('name', ''))
tc = m.get('tool_calls', [])
for t in tc:
    fn = t.get('function', {})
    if fn.get('name') == 'project_filesystem':
        args_str = fn.get('arguments', '')
        args = json.loads(args_str)
        print('action=' + args.get('action', ''))
        print('file=' + args.get('file', ''))
        print('start_line=' + str(args.get('start_line', 'N/A')))
        print('end_line=' + str(args.get('end_line', 'N/A')))
        print('start_offset=' + str(args.get('start_offset', 'N/A')))
        print('length=' + str(args.get('length', 'N/A')))
