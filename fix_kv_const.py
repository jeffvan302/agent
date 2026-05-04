import os

def read_file(path):
    with open(path, 'rb') as f:
        raw = f.read()
    if raw[:2] == b'\xff\xfe':
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

# Fix const auto& kv -> auto& kv in both functions
old1 = "    for (const auto\u0026 kv : map) {"
new1 = "    for (auto\u0026 kv : map) {"
count = text.count(old1)
text = text.replace(old1, new1)
print(f'Replaced {count} occurrences of "const auto\u0026 kv" with "auto\u0026 kv"')

write_file('src/openai_client.cpp', text, eol, bom)
print('Done')
