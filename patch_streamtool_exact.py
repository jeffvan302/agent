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
    return text, eol, bom

def write_file(path, text, eol, bom):
    if bom:
        b = text.encode('utf-16-le')
    else:
        b = text.encode('utf-8')
    with open(path, 'wb') as f:
        f.write(b)

text, eol, bom = read_file('src/openai_client.cpp')

old = (
    "ChatCompletionResult OpenAIClient::StreamToolAwareCompletion(const ChatRequestOptions& request," + eol +
    "                                                             const std::vector<ChatToolDefinition>& tools," + eol +
    "                                                             const std::function<void(const std::string&)>& on_delta," + eol +
    "                                                             const std::function<void(const ProviderQueueStatus&)>& on_queue_status," + eol +
    "                                                             const std::function<void(const std::string&, const std::string&)>& on_activity_status) {" + eol +
    "    GateSlot slot(request, GateDomain::Chat);"
)
new = (
    "ChatCompletionResult OpenAIClient::StreamToolAwareCompletion(const ChatRequestOptions& request," + eol +
    "                                                             const std::vector<ChatToolDefinition>& tools," + eol +
    "                                                             const std::function<void(const std::string&)>& on_delta," + eol +
    "                                                             const std::function<void(const ProviderQueueStatus&)>& on_queue_status," + eol +
    "                                                             const std::function<void(const std::string&, const std::string&)>& on_activity_status) {" + eol +
    "    // Binding bypass: resolve to concrete target and acquire its gate directly" + eol +
    "    if (request.model.is_binding_model && request.model.bypass_queue && request.binding_depth < 8) {" + eol +
    "        ChatRequestOptions resolved = request;" + eol +
    "        std::vector<ProviderConfig> all_providers;" + eol +
    "        {" + eol +
    "            std::lock_guard<std::mutex> lock(GateMapMutex());" + eol +
    "            for (const auto& kv : s_gate_provider_cache) all_providers.push_back(kv.second);" + eol +
    "        }" + eol +
    "        if (ResolveBindingTarget(all_providers, request.model, request, &resolved, on_queue_status)) {" + eol +
    "            return StreamToolAwareCompletion(resolved, tools, on_delta, on_queue_status, on_activity_status);" + eol +
    "        }" + eol +
    "        ChatCompletionResult fail;" + eol +
    "        fail.error = \"Binding resolution failed: no target available for \" + request.model.id;" + eol +
    "        return fail;" + eol +
    "    }" + eol +
    "    GateSlot slot(request, GateDomain::Chat);"
)

if old in text:
    text = text.replace(old, new)
    print('Patched StreamToolAwareCompletion')
else:
    print('WARN: old not found')

write_file('src/openai_client.cpp', text, eol, bom)
print('Done')
