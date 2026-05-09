import json
with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)
for i in range(614, 621):
    m = msgs[i]
    role = m.get('role')
    name = m.get('name', '')
    content_len = len(m.get('content', ''))
    tcj = m.get('tool_calls_json', '')[:200]
    tcid = m.get('tool_call_id', '')
    print(f'--- [{i}] role={role} name={name} ---')
    print(f'content_len={content_len}')
    print(f'tool_calls_json={tcj}')
    print(f'tool_call_id={tcid}')
    print()
