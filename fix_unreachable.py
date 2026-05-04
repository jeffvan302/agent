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
    "    const BindingTargetConfig* selected = nullptr;" + eol +
    "    if (round_robin) {" + eol +
    "        static std::unordered_map\u003cstd::string, size_t\u003e rr_index;" + eol +
    "        static std::mutex rr_mtx;" + eol +
    "        std::lock_guard\u003cstd::mutex\u003e lock(rr_mtx);" + eol +
    "        size_t\u0026 idx = rr_index[binding_model.id];" + eol +
    "        for (size_t attempt = 0; attempt \u003c candidates.size(); ++attempt) {" + eol +
    "            const auto* candidate = candidates[idx % candidates.size()];" + eol +
    "            idx++;" + eol +
    "            selected = candidate;" + eol +
    "            break;" + eol +
    "        }" + eol +
    "    } else {" + eol +
    "        for (const auto* candidate : candidates) {" + eol +
    "            selected = candidate;" + eol +
    "            break;" + eol +
    "        }" + eol +
    "    }"
)
new = (
    "    const BindingTargetConfig* selected = nullptr;" + eol +
    "    if (round_robin \u0026\u0026 !candidates.empty()) {" + eol +
    "        static std::unordered_map\u003cstd::string, size_t\u003e rr_index;" + eol +
    "        static std::mutex rr_mtx;" + eol +
    "        std::lock_guard\u003cstd::mutex\u003e lock(rr_mtx);" + eol +
    "        size_t\u0026 idx = rr_index[binding_model.id];" + eol +
    "        selected = candidates[idx % candidates.size()];" + eol +
    "        idx++;" + eol +
    "    } else if (!candidates.empty()) {" + eol +
    "        selected = candidates.front();" + eol +
    "    }"
)
if old in text:
    text = text.replace(old, new)
    print('Fixed ResolveBindingTarget unreachable code')
else:
    print('WARN: old not found')

write_file('src/openai_client.cpp', text, eol, bom)
print('Done')
