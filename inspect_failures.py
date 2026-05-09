import json

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

failure_indices = [302, 308, 312, 413, 419, 423, 440]

for fi in failure_indices:
    print('=== Failure at message [' + str(fi) + '] ===')
    print('Result: ' + msgs[fi].get('content', '')[:200])
    
    # Find the assistant message that made this tool call
    ai_idx = fi - 1
    while ai_idx >= 0 and msgs[ai_idx].get('role') != 'assistant':
        ai_idx -= 1
    
    if ai_idx >= 0:
        tcj = msgs[ai_idx].get('tool_calls_json', '')
        if tcj:
            try:
                tcs = json.loads(tcj)
                for tc in tcs:
                    fn = tc.get('function', {})
                    if fn.get('name') == 'project_filesystem':
                        args = json.loads(fn.get('arguments', '{}'))
                        action = args.get('action', '')
                        file_path = args.get('file', '')
                        old_lines = args.get('old_lines', [])
                        new_lines = args.get('new_lines', [])
                        print('File: ' + file_path)
                        print('old_lines:')
                        for line in old_lines:
                            safe = repr(line)
                            print('  ' + safe)
                        print('new_lines (first 3):')
                        for line in new_lines[:3]:
                            safe = repr(line)
                            print('  ' + safe)
            except Exception as e:
                print('Parse error:', e)
    print()
