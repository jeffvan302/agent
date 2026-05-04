import codecs, sys

def read(path):
    with open(path, 'rb') as f:
        raw = f.read()
    if raw[:2] == b'\xff\xfe':
        return raw.decode('utf-16-le'), '\r\n', True
    text = raw.decode('utf-8')
    return text, '\r\n' if '\r\n' in text else '\n', False

def write(path, text, eol, bom):
    if not text.endswith(eol):
        text += eol
    with open(path, 'wb') as f:
        f.write(text.encode('utf-16-le' if bom else 'utf-8'))

t,eol,bom = read('src/ollama_api_client.cpp')

# Fix non-streaming raw_content
old_ns = '            if (j.contains("message") && j["message"].contains("content")) out_acc.response_text = j["message"].value("content", "");'
new_ns = '            if (j.contains("message") && j["message"].contains("content")) out_acc.raw_content = out_acc.response_text = j["message"].value("content", "");'
if old_ns in t:
    t = t.replace(old_ns, new_ns)
    print('Fixed non-streaming raw_content')

# Fix streaming: keep raw thinking visible by using raw_content for delta
# But only delta over previous raw_content
old_stream = '''            if (j.contains("message") && j["message"].contains("content")) {
                std::string piece = j["message"].value("content", "");
                if (!piece.empty()) {
                    acc.raw_content += piece;
                    auto [think, content] = ExtractThinkingAndContent(acc.raw_content);
                    acc.thinking_text = think;
                    if (content.size() > acc.response_text.size()) {
                        std::string delta = content.substr(acc.response_text.size());
                        acc.response_text = content;
                        if (on_delta) on_delta(delta);
                    } else if (content.size() < acc.response_text.size()) {
                        acc.response_text = content;
                    }
                }
            }'''
new_stream = '''            if (j.contains("message") && j["message"].contains("content")) {
                std::string piece = j["message"].value("content", "");
                if (!piece.empty()) {
                    acc.raw_content += piece;
                    auto [think, content] = ExtractThinkingAndContent(acc.raw_content);
                    acc.thinking_text = think;
                    // Preserve thinking tags in streamed output so they remain visible
                    std::string delta = acc.raw_content.substr(acc.response_text.size());
                    acc.response_text = acc.raw_content;
                    if (on_delta) on_delta(delta);
                }
            }'''
if old_stream in t:
    t = t.replace(old_stream, new_stream)
    print('Fixed streaming delta')

write('src/ollama_api_client.cpp', t, eol, bom)
print('Done')
