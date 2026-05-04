import os

def read_file(path):
    with open(path, 'rb') as f:
        raw = f.read()
    if raw.startswith(b'\xff\xfe'):
        text = raw.decode('utf-16-le')
        eol = '\r\n' if '\r\n' in text else '\n'
        bom = True
    else:
        text = raw.decode('utf-8')
        eol = '\r\n' if '\r\n' in text else '\n'
        bom = False
    return text.splitlines(), eol, bom

def write_file(path, lines, eol, bom):
    text = eol.join(lines)
    if bom:
        b = text.encode('utf-16-le')
    else:
        b = text.encode('utf-8')
    with open(path, 'wb') as f:
        f.write(b)

lines, eol, bom = read_file('src/openai_client.cpp')

insertion_block = [
    '    // Binding bypass: resolve to concrete target and acquire its gate directly',
    '    if (request.model.is_binding_model && request.model.bypass_queue && request.binding_depth < 8) {',
    '        ChatRequestOptions resolved = request;',
    '        std::vector<ProviderConfig> all_providers;',
    '        {',
    '            std::lock_guard<std::mutex> lock(GateMapMutex());',
    '            for (const auto& kv : s_gate_provider_cache) all_providers.push_back(kv.second);',
    '        }',
    '        if (ResolveBindingTarget(all_providers, request.model, request, &resolved, on_queue_status)) {',
    '            return StreamToolAwareCompletion(resolved, tools, on_delta, on_queue_status, on_activity_status);',
    '        }',
    '        ChatCompletionResult fail;',
    '        fail.error = "Binding resolution failed: no target available for " + request.model.id;',
    '        return fail;',
    '    }',
]

found_pos = None
for i, line in enumerate(lines):
    if 'ChatCompletionResult OpenAIClient::StreamToolAwareCompletion(const ChatRequestOptions& request,' in line:
        # look for next GateSlot inside this function
        for j in range(i + 1, min(i + 15, len(lines))):
            if 'GateSlot slot(request, GateDomain::Chat);' in lines[j]:
                found_pos = j
                break
        break

if found_pos is not None:
    for l in reversed(insertion_block):
        lines.insert(found_pos, l)
    print('Inserted StreamToolAwareCompletion bypass')
else:
    print('WARN: StreamToolAwareCompletion insertion point not found')

write_file('src/openai_client.cpp', lines, eol, bom)
print('Done')
