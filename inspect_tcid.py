import json
with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)
for j in [413, 419, 423, 440]:
    m = msgs[j]
    print('[' + str(j) + '] role=' + m.get('role') + ' name=' + m.get('name', '') + ' tcid=' + m.get('tool_call_id', ''))
    print('  content=' + repr(m.get('content', ''))[:200])
