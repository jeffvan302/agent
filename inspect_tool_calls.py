import json

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

failure_indices = [302, 308, 312, 413, 419, 423, 440]

for fi in failure_indices:
    print('=== Failure at [' + str(fi) + '] ===')
    ai_idx = fi - 1
    while ai_idx >= 0 and msgs[ai_idx].get('role') != 'assistant':
        ai_idx -= 1
    
    if ai_idx >= 0:
        tc = msgs[ai_idx].get('tool_calls', [])
        if tc:
            print('Assistant at [' + str(ai_idx) + '] has ' + str(len(tc)) + ' tool_calls')
            for t in tc:
                fn = t.get('function', {})
                if fn.get('name') == 'project_filesystem':
                    args = json.loads(fn.get('arguments', '{}'))
                    if args.get('action') == 'edit':
                        print('File: ' + args.get('file', ''))
                        print('old_lines:')
                        for line in args.get('old_lines', []):
                            print('  ' + repr(line))
                        print('new_lines (first 5):')
                        for line in args.get('new_lines', [])[:5]:
                            print('  ' + repr(line))
    print()
