import json
with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\context_debug.json', 'r', encoding='utf-8') as f:
    data = json.load(f)
entries = data['entries']
last = entries[-1]
req_msgs = last.get('request_messages', [])
print('Total request messages:', len(req_msgs))
for i, m in enumerate(req_msgs):
    role = m.get('role')
    content = m.get('content', '')[:80]
    tcj = m.get('tool_calls_json', '')[:80]
    tcid = m.get('tool_call_id', '')
    print(f'[{i}] role={role} tcid={tcid} content={content}')
    if tcj:
        print(f'    tool_calls_json={tcj}')
