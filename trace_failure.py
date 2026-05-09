import json

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

# Trace failure [302]
fi = 302
print('=== Tracing failure at [' + str(fi) + '] ===')
print('Result:', msgs[fi].get('content', '')[:200])

# The tool_call_id might help us find the matching assistant message
# But in this chat format, the assistant message has tool_calls_json and the tool result has tool_call_id
# Let's look at messages [fi-5] to [fi] to understand the flow
for j in range(fi-8, fi+1):
    m = msgs[j]
    role = m.get('role')
    name = m.get('name', '')
    tcid = m.get('tool_call_id', '')
    content_preview = m.get('content', '')[:80]
    print('[' + str(j) + '] role=' + role + ' name=' + name + ' tcid=' + tcid)
    if role == 'assistant' and m.get('tool_calls_json'):
        print('  tcj=' + m.get('tool_calls_json', '')[:200])
    if role == 'tool':
        print('  content=' + content_preview)
