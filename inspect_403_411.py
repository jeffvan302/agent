import json

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

for j in range(388, 413):
    m = msgs[j]
    if m.get('role') == 'tool' and m.get('name') == 'project_filesystem':
        content = m.get('content', '')
        tcid = m.get('tool_call_id', '')
        print('[' + str(j) + '] tcid=' + tcid)
        print('  ' + content[:100].replace('\n', '\\n'))
