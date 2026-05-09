import json

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

for i, m in enumerate(msgs):
    if m.get('name') == 'project_filesystem' and m.get('role') == 'tool':
        content = m.get('content', '')
        lc = content.lower()
        if 'not found' in lc or 'failed' in lc or 'could not' in lc or 'no match' in lc or 'match' in lc or 'edit' in lc:
            print('--- [' + str(i) + '] ---')
            print(content[:600])
            if i > 0 and msgs[i-1].get('role') == 'assistant':
                tcj = msgs[i-1].get('tool_calls_json', '')
                if tcj:
                    try:
                        tcs = json.loads(tcj)
                        for tc in tcs:
                            fn = tc.get('function', {})
                            if fn.get('name') == 'project_filesystem':
                                args = json.loads(fn.get('arguments', '{}'))
                                action = args.get('action', '')
                                file_path = args.get('file', '')
                                old_text = str(args.get('old_text', ''))[:200]
                                print('  action=' + action + ' file=' + file_path)
                                print('  old_text_preview=' + old_text)
                    except Exception as e:
                        print('  parse error:', e)
            print()
