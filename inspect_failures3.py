import json

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

failure_indices = [302, 308, 312, 413, 419, 423, 440]

for fi in failure_indices:
    print('=== Failure at [' + str(fi) + '] ===')
    for j in range(fi-1, fi-4, -1):
        m = msgs[j]
        role = m.get('role')
        name = m.get('name', '')
        tcj = m.get('tool_calls_json', '')
        print('  [' + str(j) + '] role=' + role + ' name=' + name)
        if role == 'assistant' and tcj:
            print('    tool_calls_json=' + tcj[:300])
    print()
