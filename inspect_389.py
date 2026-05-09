import json

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

m = msgs[389]
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
        content = args.get('content', '')
        print('content_len=' + str(len(content)))
        print('content first 200 chars:')
        print(repr(content[:200]))
