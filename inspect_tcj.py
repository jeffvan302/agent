import json

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

for j in [301, 307, 411]:
    m = msgs[j]
    print('=== [' + str(j) + '] ===')
    print('role=' + m.get('role'))
    print('name=' + m.get('name', ''))
    tcj = m.get('tool_calls_json', '')
    print('tool_calls_json type=' + str(type(tcj)))
    print('tool_calls_json=' + repr(tcj)[:300])
    print('content_preview=' + repr(m.get('content', ''))[:100])
    print()
