import json, sys
with open(sys.argv[1], 'r', encoding='utf-8') as f:
    entries = json.load(f)
print('Total debug entries:', len(entries))
last = entries[-1]
print('Last entry kind:', last.get('kind'))
print('Last entry created_at:', last.get('created_at'))
print('Last entry provider_id:', last.get('provider_id'))
print('Last entry model_id:', last.get('model_id'))
print('Request messages count:', len(last.get('request_messages', [])))
for i, m in enumerate(last.get('request_messages', [])[-5:]):
    role = m.get('role')
    content = m.get('content', '')[:120]
    print(f'  req_msg[{i}]: role={role} content_preview={content}')
print('System prompt preview:', last.get('system_prompt', '')[:200])
print('MCP context count:', len(last.get('mcp_context', [])))
print('RAG context count:', len(last.get('rag_context', [])))
