import json, sys

with open(r'.data\projects\project_1777616659667_10\chats\chat_1778281586640_8\messages.json', 'r', encoding='utf-8') as f:
    msgs = json.load(f)

for i, m in enumerate(msgs):
    if m.get('name') == 'project_filesystem' and m.get('role') == 'tool':
        content = m.get('content', '')
        lc = content.lower()
        # Focus on results that indicate matching problems
        if any(x in lc for x in ['not found', 'no match', 'failed', 'could not', 'already applied', 'multiple matches', 'normalized multiline']):
            # Get the assistant message before this (which contains the tool call arguments)
            assistant_idx = i - 1
            while assistant_idx >= 0 and msgs[assistant_idx].get('role') != 'assistant':
                assistant_idx -= 1
            tcj = msgs[assistant_idx].get('tool_calls_json', '') if assistant_idx >= 0 else ''
            action = ''
            file_path = ''
            old_text = ''
            if tcj:
                try:
                    tcs = json.loads(tcj)
                    for tc in tcs:
                        fn = tc.get('function', {})
                        if fn.get('name') == 'project_filesystem':
                            args = json.loads(fn.get('arguments', '{}'))
                            action = args.get('action', '')
                            file_path = args.get('file', '')
                            old_text = args.get('old_text', '')
                except:
                    pass
            print('--- [' + str(i) + '] action=' + action + ' file=' + file_path + ' ---')
            # Print the result content (first 300 chars)
            safe_content = content.encode('ascii', 'replace').decode('ascii')
            print(safe_content[:300])
            if old_text:
                safe_old = old_text.encode('ascii', 'replace').decode('ascii')
                print('old_text_preview: ' + safe_old[:200])
            print()
