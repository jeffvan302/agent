import json

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

failure_indices = [302, 308, 312, 413, 419, 423, 440]

for fi in failure_indices:
    print('=== Failure at message [' + str(fi) + '] ===')
    tcid = msgs[fi].get('tool_call_id', '')
    # Find assistant message with this tool_call_id in its tool_calls_json
    for j in range(fi - 1, max(0, fi - 10), -1):
        if msgs[j].get('role') == 'assistant':
            tcj = msgs[j].get('tool_calls_json', '')
            if tcj and tcid in tcj:
                print('Found assistant at [' + str(j) + ']')
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
                break
    print()
