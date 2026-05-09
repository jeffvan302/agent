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
            print('=== [' + str(idx) + '] keys=' + str(list(args.keys())) + ' ===')
            print('action=' + args.get('action', ''))
            print('path=' + repr(args.get('path', '')))
            print('file=' + repr(args.get('file', '')))
            print()
