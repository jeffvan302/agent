import pathlib

root = pathlib.Path(r'C:\Users\TheunisvanNiekerk\Code\agent')
src = root / 'src' / 'web_assets_default.cpp'

with open(src, 'rb') as f:
    cpp_bytes = f.read()

cpp_text = cpp_bytes.decode('utf-8-sig')
line_end = '\r\n' if b'\r\n' in cpp_bytes else '\n'

def replace_last_between(text, prefix, suffix, new_content):
    idx1 = text.rfind(prefix)
    if idx1 == -1:
        raise ValueError(f'Prefix not found: {repr(prefix[:80])}')
    idx2 = text.find(suffix, idx1 + len(prefix))
    if idx2 == -1:
        raise ValueError(f'Suffix not found after prefix')
    return text[:idx1 + len(prefix)] + new_content + text[idx2:]

# Update index.html
with open(root / 'www' / 'index.html', 'r', encoding='utf-8') as f:
    html = f.read()
cpp_text = replace_last_between(
    cpp_text,
    f'const char kIndexHtml[] ={line_end}R"ASSET({line_end}',
    f'{line_end})ASSET";{line_end}',
    html,
)

# Update base.css
with open(root / 'www' / 'css' / 'base.css', 'r', encoding='utf-8') as f:
    css = f.read()
cpp_text = replace_last_between(
    cpp_text,
    f'const char kBaseCss[] ={line_end}R"ASSET({line_end}',
    f'{line_end})ASSET";{line_end}',
    css,
)

# Update app.js — it stores app.js split into an array of parts.
with open(root / 'www' / 'js' / 'app.js', 'r', encoding='utf-8') as f:
    js = f.read()
cpp_text = replace_last_between(
    cpp_text,
    f'const char* const kAppJsParts[] = {{{line_end}R"ASSET({line_end}',
    f'{line_end})ASSET"{line_end}}};',
    js,
)

with open(src, 'w', encoding='utf-8', newline='') as f:
    f.write(cpp_text)

print(f'Updated web_assets_default.cpp with latest www assets (line endings: {repr(line_end)}).')
