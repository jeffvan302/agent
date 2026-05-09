import json

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

failure_indices = [302, 308, 312, 413, 419, 423, 440]

for fi in failure_indices:
    print('=== Failure at [' + str(fi) + '] ===')
    # Search backwards up to 10 messages for an assistant with project_filesystem edit
    found = False
    for ai_idx in range(fi - 1, max(0, fi - 15), -1):
        if msgs[ai_idx].get('role') == 'assistant':
            tcj = msgs[ai_idx].get('tool_calls_json', '')
            if tcj and 'project_filesystem' in tcj:
                try:
                    tcs = json.loads(tcj)
                    for tc in tcs:
                        fn = tc.get('function', {})
                        if fn.get('name') == 'project_filesystem':
                            args = json.loads(fn.get('arguments', '{}'))
                            if args.get('action') == 'edit':
                                print('Found at assistant [' + str(ai_idx) + ']')
                                print('File: ' + args.get('file', ''))
                                print('old_lines:')
                                for line in args.get('old_lines', []):
                                    print('  ' + repr(line))
                                print('new_lines (first 5):')
                                for line in args.get('new_lines', [])[:5]:
                                    print('  ' + repr(line))
                                found = True
                                break
                except Exception as e:
                    pass
            if found:
                break
    if not found:
        print('No project_filesystem edit tool call found in preceding messages')
    print()
