import json, sys

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

# Look for consecutive edit attempts on the same file
last_file = ''
last_action = ''
edit_counts = {}
for i, m in enumerate(msgs):
    if m.get('role') == 'assistant':
        tcj = m.get('tool_calls_json', '')
        if tcj:
            try:
                tcs = json.loads(tcj)
                for tc in tcs:
                    fn = tc.get('function', {})
                    if fn.get('name') == 'project_filesystem':
                        args = json.loads(fn.get('arguments', '{}'))
                        action = args.get('action', '')
                        file_path = args.get('file', '')
                        if action == 'edit' and file_path:
                            edit_counts[file_path] = edit_counts.get(file_path, 0) + 1
            except:
                pass

print('Edit counts per file:')
for f, c in sorted(edit_counts.items(), key=lambda x: -x[1]):
    print('  ' + f + ': ' + str(c))

# Now look for specific failure messages
print('\n--- Looking for failure results ---')
for i, m in enumerate(msgs):
    if m.get('name') == 'project_filesystem' and m.get('role') == 'tool':
        content = m.get('content', '')
        lc = content.lower()
        if 'failed' in lc or 'not found' in lc or 'no match' in lc or 'could not' in lc:
            safe_content = content.encode('ascii', 'replace').decode('ascii')
            print('[' + str(i) + ']: ' + safe_content[:200])
