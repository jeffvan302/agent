import json

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

for j in [612, 613, 614]:
    m = msgs[j]
    print('=== [' + str(j) + '] ===')
    print('role=' + m.get('role'))
    print('name=' + m.get('name', ''))
    print('tool_call_id=' + m.get('tool_call_id', ''))
    print('content_preview=' + repr(m.get('content', ''))[:200])
    print('tool_calls_json=' + repr(m.get('tool_calls_json', ''))[:300])
    print()
