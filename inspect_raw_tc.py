import json

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

for j in [301, 307, 412, 418, 422, 439]:
    m = msgs[j]
    print('=== [' + str(j) + '] ===')
    print('role=' + m.get('role'))
    print('name=' + m.get('name', ''))
    tc = m.get('tool_calls', [])
    print('tool_calls count=' + str(len(tc)))
    for i, t in enumerate(tc):
        print('  tool_call[' + str(i) + ']:')
        print('    id=' + t.get('id', ''))
        print('    type=' + t.get('type', ''))
        fn = t.get('function', {})
        print('    function.name=' + fn.get('name', ''))
        print('    function.arguments=' + repr(fn.get('arguments', '')[:300]))
    print()
